all: server client

server: server.cpp chat.pb.cc
	g++ -o server server.cpp chat.pb.cc -lpthread -lprotobuf

client: client.cpp chat.pb.cc
	g++ -o client client.cpp chat.pb.cc -lpthread -lprotobuf

chat.pb.cc: chat.proto
	protoc -I=. --cpp_out=. chat.proto