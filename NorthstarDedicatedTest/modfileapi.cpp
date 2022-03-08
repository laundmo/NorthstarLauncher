#include "pch.h"
#include "squirrel.h"
#include <iostream>
#include <fstream>
#include <string>

using namespace std;

string readFile(const string& fileName)
{
	string data;
	ifstream in(fileName.c_str());
	getline(in, data, string::traits_type::to_char_type(string::traits_type::eof()));
	return data;
}

string getLine(string path, int linenum) {
	string line;
	std::ifstream infile(path.c_str(), ios::in);
	for (int i = 1; i <= linenum; i++)
	{
		getline(infile, line);
	}
	return line;
}

void removeLine(string path, int linenum) {
	ifstream infile(path.c_str(), ios::in);
	string tmppath = path + ".tmp";
	ofstream temp(tmppath.c_str(), ios::out);
	string line;
	int i = 0;
	while (getline(infile, line))
	{
		i++;
		if (i != linenum)
		{
			temp << line + "\n";
		}
	}

	infile.close();
	temp.close();
	remove(path.c_str());
	rename(tmppath.c_str(), path.c_str());
}

SQRESULT SQ_ReadFile(void* sqvm) {
	string path = ServerSq_getstring(sqvm, 1);
	string content = readFile(path.c_str());
	ServerSq_pushstring(sqvm, content.c_str(), -1);
	return SQRESULT_NOTNULL;
}

SQRESULT SQ_AppendFile(void* sqvm) {
	string path = ServerSq_getstring(sqvm, 1);
	string content = ServerSq_getstring(sqvm, 2);
	ofstream outfile(path, ios::app | ios::out);
	outfile << content + "\n";
	return SQRESULT_NULL;
}

SQRESULT SQ_ReadFileLine(void* sqvm) {
	string path = ServerSq_getstring(sqvm, 1);
	int linenum = ServerSq_getinteger(sqvm, 2);
	ServerSq_pushstring(sqvm, getLine(path, linenum).c_str(), -1);
	return SQRESULT_NOTNULL;
}

SQRESULT SQ_RemoveFileLine(void* sqvm)
{
	string path = ServerSq_getstring(sqvm, 1);
	int linenum = ServerSq_getinteger(sqvm, 2);
	removeLine(path, linenum);
	return SQRESULT_NULL;
}

SQRESULT SQ_ClearFile(void* sqvm) {
	string path = ServerSq_getstring(sqvm, 1);
	ofstream ofs(path.c_str(), ios::out | ios::trunc);
	return SQRESULT_NULL;
}

void InitialiseModFileApiServer(HMODULE baseAddress) {
	//                                         returntype           name                     args          ?    func
	
	// whole file operations
	g_ServerSquirrelManager->AddFuncRegistration("string", "ReadFile", "string filePath", "", SQ_ReadFile);
	g_ServerSquirrelManager->AddFuncRegistration("void", "AppendFile", "string filePath, string fileContent", "", SQ_AppendFile);
	g_ServerSquirrelManager->AddFuncRegistration("string", "ClearFile", "string filePath", "", SQ_ClearFile);
	
	// line based operations
	g_ServerSquirrelManager->AddFuncRegistration("string", "ReadFileLine", "string filePath, int lineno", "", SQ_ReadFileLine);
	g_ServerSquirrelManager->AddFuncRegistration("void", "RemoveFileLine", "string filePath, int lineno", "", SQ_RemoveFileLine);
}