CXXFLAGS=-L/usr/local/opt/icu4c/lib -I/usr/local/opt/icu4c/include -Ideps/variant/include -Ideps/rapidjson-master/include -g
LIBS=-L/usr/local/lib -lpthread -lz -lexpat -lboost_filesystem-mt -lboost_system-mt -lboost_chrono-mt -lboost_regex-mt -lboost_exception-mt -lmapnik

all: simpletiles

simpletiles: src/server.cpp src/server_http.hpp src/LRUCache11.hpp
	g++ $(CXXFLAGS) -std=c++14 -o simpletiles src/server.cpp $(LIBS)

clean:
	rm simpletiles
