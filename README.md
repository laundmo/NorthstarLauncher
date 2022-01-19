# NorthstarLauncher
Launcher used to modify Titanfall 2 to allow Northstar mods and custom content to be loaded.

## Build

Check [BUILD.md](https://github.com/R2Northstar/NorthstarLauncher/blob/main/BUILD.md) for instructions on how to compile, you can also download [binaries built by GitHub Actions](https://github.com/R2Northstar/NorthstarLauncher/actions).


## Socket Commands

This is a fork that includes a TCP socket way to run console/squirrel on the Server, and recieve logs back through.

Ports: 
- `9999` for commands and squirrel
- `9998` for logs, server only sends

Squirrel scripts sent to the socket have to start with a line just containing `BOF` and end with a line just containing `EOF`

The sockets currently bind to any interface, and the ports are currently not configurable. They also dont require any authentication, so make sure that they are not port forwarded or otherwise available publically.
