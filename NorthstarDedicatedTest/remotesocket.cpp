#include <winsock2.h>
#include "spdlog/details/null_mutex.h"
#include <mutex>
#include "pch.h"
#include "dedicated.h"
#include "hookutils.h"
#include "gameutils.h"
#include "serverauthentication.h"
#include <spdlog/details/circular_q.h>
#include <spdlog/details/log_msg_buffer.h>
#include "spdlog/sinks/base_sink.h"
#include "spdlog/common.h"
#include <concurrent_queue.h>
#include "squirrel.h"
#include <algorithm>
#include <string>
#include "rapidjson/error/en.h"
#include "rapidjson/document.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/writer.h"

Concurrency::concurrent_queue<std::string> outgoingSocketString;
Concurrency::concurrent_queue<std::string> commandResultString;
Concurrency::concurrent_queue<std::string> scriptRunQueue;

bool send_to_command_result = false;

void unsafePush(Concurrency::concurrent_queue<std::string> &queue, std::string content) {
	if (queue.unsafe_size() > 2000) {
		std::string outstring;
		queue.try_pop(outstring);
		outstring.clear();
	}
	queue.push(content);
}

void PushStringToSocket(std::string send) {
	unsafePush(outgoingSocketString, send);
	if (send_to_command_result) {
		unsafePush(commandResultString, send);
	}
}

std::string replaceAll(std::string str, const std::string& from, const std::string& to)
{
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos)
	{
		str.replace(start_pos, from.length(), to);
		start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
	}
	return str;
}

template<typename Mutex> class socket_sink : public spdlog::sinks::base_sink <Mutex>
{
protected:

	void sink_it_(const spdlog::details::log_msg& msg) override
	{
		spdlog::memory_buf_t formatted;
		spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
		rapidjson_document log_msg;
		log_msg.SetObject();
		log_msg.AddMember("typ", "log", log_msg.GetAllocator());
		log_msg.AddMember("message", fmt::to_string(formatted), log_msg.GetAllocator());
		rapidjson::StringBuffer buffer;
		buffer.Clear();
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		log_msg.Accept(writer);
		PushStringToSocket(std::string(buffer.GetString()) + "\n");
	}

	void flush_() override {}
};

using socket_sink_mt = socket_sink<std::mutex>;
// using socket_sink_st = socket_sink<spdlog::details::null_mutex>;

extern std::shared_ptr<socket_sink_mt> sink = std::make_shared<socket_sink_mt>();

void setSocketSink() {
	spdlog::default_logger()->sinks().push_back(sink);
}


HANDLE socketInputThreadHandle = NULL;

bool setupListenSocket(SOCKET& acceptSocket, int port) {
	long rc;
	SOCKADDR_IN addr;
	WSADATA wsa;
	rc = WSAStartup(MAKEWORD(2, 0), &wsa);
	if (rc != 0)
	{
		spdlog::info("Failed to create socket. errno: {}", rc);
	}
	acceptSocket = socket(AF_INET, SOCK_STREAM, 0);

	if (acceptSocket == INVALID_SOCKET)
	{
		spdlog::info("Inavlid socket.");
		closesocket(acceptSocket);
		WSACleanup();
		return false;
	}

	memset(&addr, 0, sizeof(SOCKADDR_IN));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = ADDR_ANY;
	rc = bind(acceptSocket, (SOCKADDR*)&addr, sizeof(SOCKADDR_IN));
	if (rc == SOCKET_ERROR)
	{
		spdlog::info("socket bind failed, errno: {}", WSAGetLastError());
		closesocket(acceptSocket);
		WSACleanup();
		return false;
	}
	rc = listen(acceptSocket, 10);
	if (rc == SOCKET_ERROR)
	{
		spdlog::info("socket listen failed, errno: {}", WSAGetLastError());
		closesocket(acceptSocket);
		WSACleanup();
		return false;
	}
	return true;
}

DWORD WINAPI SocketInputThread(PVOID pThreadParameter) {
	while (!g_pEngine || !g_pHostState || g_pHostState->m_iCurrentState != HostState_t::HS_RUN)
		Sleep(1000);
	SOCKET listenSocket;
	if (!setupListenSocket(listenSocket, 9999)) {
		return 0;
	}
	SOCKET connectedSocket;

	while (g_pEngine && g_pEngine->m_nQuitting == EngineQuitState::QUIT_NOTQUITTING) {
		connectedSocket = accept(listenSocket, NULL, NULL);
		if (connectedSocket == INVALID_SOCKET) {
			spdlog::info("issue accepting socket: {}", WSAGetLastError());
			closesocket(connectedSocket);
			continue;
		}
		//while true
		//    while \n is not in recieved
		//        recv more
		//    get first line (until \n)
		//    execute line
		long len = 1;
		bool is_script = false;
		std::string script;
		std::string receive;
		do {
			while (receive.find('\n') == std::string::npos) {
				char buf[1024];
				memset(buf, 0, sizeof buf);
				len = recv(connectedSocket, buf, 1024, 0);
				if (len <= 0) break;
				spdlog::info("socket recv: {}", buf);
				receive += buf;
			}
			spdlog::info("recv loop stopped");
			if (len <= 0) {
				spdlog::info("len = 0, break");
				break;
			}
			// get line till newline
			int sep;
			sep = receive.find("\n");
			spdlog::info("sep: {}", sep);
			std::string line = receive.substr(0, sep);
			spdlog::info("line: {}", line);
			receive = receive.substr(sep + 1, receive.length());
			spdlog::info("leftover: {}", receive);

			// START RUN LINE
			if (is_script) {
				if (line == "EOF") {
					spdlog::info("EOF, running command");
					is_script = false;
					unsafePush(scriptRunQueue, script);
					script.clear();
				}
				else {
					script += line;
				}
			}
			else {
				if (line == "BOF") {
					spdlog::info("BOF, start file");
					is_script = true;
				}
				else {
					line += '\n';
					Cbuf_AddText(Cbuf_GetCurrentPlayer(), line.c_str(), cmd_source_t::kCommandSrcCode);
				}

			}
			// END RUN LINE

		} while (len > 0);
		long res;
		res = shutdown(connectedSocket, SD_SEND);
		closesocket(connectedSocket);
	}
	closesocket(listenSocket);
	WSACleanup();
	return 0;
}


HANDLE socketOutputThreadHandle = NULL;

DWORD WINAPI SocketOutputThread(PVOID pThreadParameter) {
	while (!g_pEngine || !g_pHostState || g_pHostState->m_iCurrentState != HostState_t::HS_RUN)
		Sleep(1000);
	SOCKET listenSocket;
	if (!setupListenSocket(listenSocket, 9998)) {
		return 0;
	}
	SOCKET connectedSocket;

	while (g_pEngine && g_pEngine->m_nQuitting == EngineQuitState::QUIT_NOTQUITTING) {
		connectedSocket = accept(listenSocket, NULL, NULL);
		if (connectedSocket == INVALID_SOCKET) {
			spdlog::info("issue accepting socket: {}", WSAGetLastError());
			closesocket(connectedSocket);
			continue;
		}
		long res;
		do {
			std::string outstring;
			if (outgoingSocketString.try_pop(outstring)) {
				res = send(connectedSocket, outstring.c_str(), outstring.size(), 0);
			}
			else {
				Sleep(5);
			}
		} while (res != SOCKET_ERROR);
		shutdown(connectedSocket, SD_SEND);
		closesocket(connectedSocket);
	}
	return 0;

}

void CallQueuedSquirrel() {
	std::string script;
	if (scriptRunQueue.try_pop(script)){
		g_ServerSquirrelManager->ExecuteCode(script.c_str());
		script.clear();
	}
}

void SetupSocketRemote() {
	socketInputThreadHandle = CreateThread(0, 0, SocketInputThread, 0, 0, NULL);
	socketOutputThreadHandle = CreateThread(0, 0, SocketOutputThread, 0, 0, NULL);
	setSocketSink();
}