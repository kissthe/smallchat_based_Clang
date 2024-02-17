cmake_minimum_required(VERSION 3.10)

project(ServerClient)

# 添加 chatlib.c 到一个库中，以便 Server 和 Client 共享
add_library(chatlib chatlib.c)

# 生成 Server 可执行程序
add_executable(Server server.c)
target_link_libraries(Server PRIVATE chatlib)

# 生成 Client 可执行程序
add_executable(Client client.c)
target_link_libraries(Client PRIVATE chatlib)
