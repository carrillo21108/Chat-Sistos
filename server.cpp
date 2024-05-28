#include <iostream>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include "chat.pb.h"

#include <chrono>
#include <thread>
#include <mutex>


using namespace std;

#define length 8192

// Tiempo de inactividad en segundos (modificable)
int inactivity_timeout = 60;

// Mutex para acceso a la lista de clientes
std::mutex clients_mutex;

// Struct of user: para modelar el socket por el que se conecta, un username, su ip y su status basandose en protoc
struct User{
    int socketFd;
    std::string username;
    std::string ip;
    chat::UserStatus status;
    std::chrono::time_point<std::chrono::steady_clock> last_active;
};

// Client list
std::unordered_map<std::string, User*> clients;

// Actualizacion de hora de actividad de usuario
void updateUserActivity(User* user) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    user->last_active = std::chrono::steady_clock::now();
}

// Funcion para verificacion de tiempo de actividad de los usuarios
void *checkInactiveClients(void* arg) {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10)); // Verificar cada 10 segundos
        
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto now = std::chrono::steady_clock::now();
        for (auto& client_pair : clients) {
            User* user = client_pair.second;
            if(user->status != chat::OFFLINE){
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - user->last_active).count();
                if (duration >= inactivity_timeout) {
                    user->status = chat::OFFLINE;
                    std::cout << "User " << user->username << " is now OFFLINE due to inactivity." << std::endl;
                }
            }
        }
    }

    return nullptr;
}

// Funcion para enviar errores segun el retorno del codigo
void SendErrorResponse(int socketFd, chat::Operation op, std::string error_message)
{
    std::string serialized_response;
    chat::Response *error_response = new chat::Response();
    error_response->set_operation(op);
    error_response->set_status_code(chat::BAD_REQUEST);
    error_response->set_message(error_message);
    error_response->SerializeToString(&serialized_response);
    
    send(socketFd, serialized_response.data(), serialized_response.size(), 0);
}


// ---------------------------- Recibir y enviar respuestas ----------------------------
// Esta funcion se pasa a un thread para recibir y enviar mensajes serializados de los clientes que se conecten a el
// este hilo esta esperando en todo momento que le envien informacion  
void *ThreadWork(void *params)
{
    // Recibe un usuario y lo conecta a un socket
    struct User user;
    struct User *newClientParams = (struct User *)params;
    int socketFd = newClientParams->socketFd;
    std::string userIp = newClientParams->ip;
    std::vector<char> buffer(length);
	
    // Server Structs
    std::string serialized_response;
    chat::Request *request = new chat::Request();
    chat::Response *response = new chat::Response();
	
    while (1){
        response->Clear();
	
	// Si recv devuelve 0, se desconecta al usuario
        ssize_t bytesRead = recv(socketFd, buffer.data(), buffer.size(), 0);
        if (bytesRead < 1){
            if (bytesRead == 0){
                std::cout<<std::endl<<"__LOGGING OUT__\n"<< "The client named: "<< user.username<< " has logged out"<< std::endl;
				clients.erase(user.username);
                close(user.socketFd);
            }
            break;
        }
	
	// Sino, otra opcion
        request->ParseFromArray(buffer.data(),bytesRead);
		switch (request->operation()){
            // Registro de usuario
			case chat::REGISTER_USER:{
				std::cout<<std::endl<<"__RECEIVED INFO__\nUsername: "<<request->register_user().username()<< std::endl;
				if (clients.count(request->register_user().username()) > 0)
				{
					std::cout<<std::endl<< "ERROR: Username already exists" <<std::endl;
					SendErrorResponse(socketFd, chat::REGISTER_USER, "ERROR: Username already exists");
					break;
				}
				response->set_operation(chat::REGISTER_USER);
				response->set_message("SUCCESS: register");
				response->set_status_code(chat::OK);
				response->SerializeToString(&serialized_response);

				send(socketFd, serialized_response.data(), serialized_response.size(), 0);
				std::cout<<std::endl<<"SUCCESS:The user"<<user.username<<" was added with the socket: "<<socketFd<<std::endl;
				user.username = request->register_user().username();
				user.socketFd = socketFd;
                user.ip = userIp;

                // Status ONLINE por defecto
				user.status = chat::ONLINE;
				clients[user.username] = &user;
                updateUserActivity(&user);
				break;
			}
            // Envio de mensaje
            case chat::SEND_MESSAGE:{
                // Broadcast
                if(request->send_message().recipient().empty()){
                    std::cout<<"\n__SENDING GENERAL MESSAGE__\nUser: "<<user.username<<" is trying to send a general message";
                    for (auto i:clients){
                        if (i.first==user.username){
                            response->set_operation(chat::SEND_MESSAGE);
                            response->set_message("\nSUCCESS: You have sent a general message\n");
                            response->set_status_code(chat::OK);
                            response->SerializeToString(&serialized_response);

                            send(socketFd, serialized_response.data(), serialized_response.size(), 0);
                            std::cout<<"\nSUCCESS:User: "+user.username+" has sent the message successfully to the general chat\n";
                        }
                        else{
                            // Si el usario esta inactivo, no recibe el mensaje
                            if(i.second->status!=chat::OFFLINE){
                                chat::IncomingMessageResponse *message = response->mutable_incoming_message();
                                message->set_sender(user.username);
                                message->set_content(request->send_message().content());
                                message->set_type(chat::BROADCAST);

                                response->set_operation(chat::INCOMING_MESSAGE);
                                //response->set_allocated_incoming_message(message);
                                response->set_message("\nUser: "+response->incoming_message().sender()+" sends you a message\n");
                                response->set_status_code(chat::OK);
                                response->SerializeToString(&serialized_response);

                                send(i.second->socketFd, serialized_response.data(), serialized_response.size(), 0);
                            }
                        }
                    }
                }
                // Mensaje directo
                else{
                    std::cout<<"\n__SENDING PRIVATE MESSAGE__\nUser: "<<user.username<<" is trying to send a private message to ->"<<request->send_message().recipient();
                    if(clients.count(request->send_message().recipient()) > 0){
                        
                        if(request->send_message().recipient()==user.username){
                            std::cout<<std::endl<< "\nERROR:\nUser: "+user.username+" tried to send a message to himself\n" <<std::endl;
					        SendErrorResponse(socketFd, chat::SEND_MESSAGE, "\nERROR:\nUser: "+user.username+" tried to send a message to himself\n");
                        }
                        else{
                            // Si el usario esta inactivo, no recibe el mensaje
                            if(clients[request->send_message().recipient()]->status!=chat::OFFLINE){
                                chat::IncomingMessageResponse *message = response->mutable_incoming_message();
                                message->set_sender(user.username);
                                message->set_content(request->send_message().content());
                                message->set_type(chat::DIRECT);

                                response->set_operation(chat::INCOMING_MESSAGE);
                                //response->set_allocated_incoming_message(message);
                                response->set_message("\nUser: "+response->incoming_message().sender()+" sends you a message\n");
                                response->set_status_code(chat::OK);
                                response->SerializeToString(&serialized_response);

                                send(clients[request->send_message().recipient()]->socketFd, serialized_response.data(), serialized_response.size(), 0);

                                response->Clear();

                                response->set_operation(chat::SEND_MESSAGE);
                                response->set_message("\nSUCCESS: You have sent the private message to "+request->send_message().recipient()+" successfully\n");
                                response->set_status_code(chat::OK);
                                response->SerializeToString(&serialized_response);
                                
                                send(socketFd, serialized_response.data(), serialized_response.size(), 0);
                                std::cout<<"\nSUCCESS:User: "+user.username+" has sent the message successfully ->"+request->send_message().recipient()+"\n";
                            }
                            else{
                                std::cout<<std::endl<< "\nERROR:\nUser: "+user.username+" tried to send a message to an offline user ->"+request->send_message().recipient()+"\n" <<std::endl;
					            SendErrorResponse(socketFd, chat::SEND_MESSAGE, "\nERROR:\nUser: "+user.username+" tried to send a message to an offline user ->"+request->send_message().recipient()+"\n");
                            }
                        }
                    }
                    else{
                        std::cout<<std::endl<< "\nERROR:\nUser: "+user.username+" tried to send a message to an unexisting user ->"+request->send_message().recipient()+"\n" <<std::endl;
					    SendErrorResponse(socketFd, chat::SEND_MESSAGE, "\nERROR:\nUser: "+user.username+" tried to send a message to an unexisting user ->"+request->send_message().recipient()+"\n");
                    }
                }
                user.status = chat::ONLINE;
                updateUserActivity(&user);
				break;
            }
            // Informacion de usuario
            case chat::GET_USERS:{
                // Informacion de todos los usuarios
                if(request->get_users().username().empty()){
                    std::cout<<"\n__ALL CONNECTED CLIENTS__\nUser: "<<user.username<<" is trying to show all users info";
                    chat::UserListResponse *list = response->mutable_user_list();
                    for(auto i:clients){
                        chat::User *userI = new chat::User();
                        // Concatenacion de username e ip
                        userI->set_username(i.second->username+" IP: "+i.second->ip);
                        userI->set_status(i.second->status);
                        list->add_users()->CopyFrom(*userI);
                    }
                    list->set_type(chat::ALL);

                    response->set_operation(chat::GET_USERS);
                    response->set_message("SUCCESS: userinfo of all connected clients");
                    //response->set_allocated_user_list(list);
                    response->set_status_code(chat::OK);
                    response->SerializeToString(&serialized_response);

                    send(socketFd, serialized_response.data(), serialized_response.size(), 0);
                    std::cout<<"\nSUCCESS:User:"<<user.username<<" has deployed info of all connected clients\n";
                }
                // Informacion de un usuario en especifico
                else{
                    std::cout<<"\n__SINGLE CONNECTED CLIENT__\nUser: "<<user.username<<" is trying to show single user info";
                    chat::UserListResponse *list = response->mutable_user_list();
                    chat::User *userI = new chat::User();

                    if(clients.count(request->get_users().username()) > 0){
                        // Concatenacion de username e ip
                        userI->set_username(clients[request->get_users().username()]->username+" IP: "+clients[request->get_users().username()]->ip);
                        userI->set_status(clients[request->get_users().username()]->status);
                        list->add_users()->CopyFrom(*userI);

                        list->set_type(chat::SINGLE);

                        response->set_operation(chat::GET_USERS);
                        response->set_message("\nSUCCESS: Userinfo of "+request->get_users().username()+" successfully\n");
                        //response->set_allocated_user_list(list);
                        response->set_status_code(chat::OK);
                        response->SerializeToString(&serialized_response);

                        send(socketFd, serialized_response.data(), serialized_response.size(), 0);
                        std::cout<<"SUCCESS:User:"<<user.username<<" has deployed userinfo successfully ->"+request->get_users().username()+"\n";
                    }
                    else{
                        std::cout<<std::endl<< "\nERROR:\nUser: "+user.username+" tried to show info of an unexisting user ->"+request->get_users().username()+"\n" <<std::endl;
					    SendErrorResponse(socketFd, chat::GET_USERS, "\nERROR:\nUser: "+user.username+" tried to show info of an unexisting user ->"+request->get_users().username()+"\n");
                    }
                }
                user.status = chat::ONLINE;
                updateUserActivity(&user);
				break;
            }
            // Actualizacion de status
            case chat::UPDATE_STATUS:{
                clients[request->update_status().username()]->status = request->update_status().new_status();

                std::cout<<"\n__USER CHANGE STATUS SOLICITUDE__\nUser: "<<user.username<<" requested to change status.\nSUCCESS:status has changed successfully\n";

                response->set_operation(chat::UPDATE_STATUS);
                response->set_message("\nSUCCESS:\nUser: "+request->update_status().username()+" has changed status successfully\n");
                response->set_status_code(chat::OK);
                response->SerializeToString(&serialized_response);

                send(socketFd, serialized_response.data(), serialized_response.size(), 0);
                updateUserActivity(&user);
				break;
            }
            // Liberacion de usuario
            case chat::UNREGISTER_USER:{
                std::cout<<std::endl<<"__LOGGING OUT__\n"<< "The client named: "<< user.username<< " has logged out"<< std::endl;

                response->set_operation(chat::UNREGISTER_USER);
                response->set_message("\nSUCCESS:\nUser: "+user.username+" has logged out\n");
                response->set_status_code(chat::OK);
                response->SerializeToString(&serialized_response);

                send(socketFd, serialized_response.data(), serialized_response.size(), 0);

				clients.erase(user.username);
                close(user.socketFd);
                break;
            }
			default:{
				break;
			}
		}
	}
    return params;
}
// ---------------------------- fin Recibir y enviar respuestas ----------------------------

// ---------------------------- Main del servidor ----------------------------
int main(int argc, char const* argv[]){
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    // Referencia: https://medium.com/@securosoft/simple-chat-server-using-sockets-in-c-f72fc8b5b24e
    // Cuando no se indica el puerto del server en parametros
    if (argc != 2){
        fprintf(stderr, "Use: server <server port>\n");
        return 1;
    }
    
    // Inicializar el server en base a los parametros ingresados
    long port = strtol(argv[1], NULL, 10);
    sockaddr_in server, incoming_conn;
    socklen_t new_conn_size;

    int socket_fd, new_conn_fd;
    char incoming_conn_addr[INET_ADDRSTRLEN];

    server.sin_family = AF_INET;
    // Definicion del puerto de escucha del server
    server.sin_port = htons(port);
    server.sin_addr.s_addr = INADDR_ANY;
    memset(server.sin_zero, 0, sizeof server.sin_zero);

    // Si hubo error al crear el socket para el cliente
    if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        fprintf(stderr, "ERROR: create socket\n");
        return 1;
    }

    // Si hubo error al crear el socket para el cliente y enlazar ip
    if (bind(socket_fd, (struct sockaddr *)&server, sizeof(server)) == -1){
        close(socket_fd);
        fprintf(stderr, "ERROR: bind IP to socket.\n");
        return 2;
    }
	
    // Si hubo error al crear el socket para esperar respuestas
    if (listen(socket_fd, 5) == -1){
        close(socket_fd);
        fprintf(stderr, "ERROR: listen socket\n");
        return 3;
    }


    // si no hubo errores se puede proceder con el listen del server
    printf("SUCCESS: listening on port-> %ld\n", port);

    // Crear el hilo de verificacion de inactividad
    pthread_t inactivity_thread;
    if (pthread_create(&inactivity_thread, nullptr, checkInactiveClients, nullptr)) {
        std::cerr << "Error creating inactivity thread" << std::endl;
        return 1;
    }
	
    while (1){
	    
        // la funcion accept nos permite ver si se reciben o envian mensajes
        new_conn_size = sizeof incoming_conn;
        new_conn_fd = accept(socket_fd, (struct sockaddr *)&incoming_conn, &new_conn_size);
	    
        // si hubo error al crear el socket para el cliente
        if (new_conn_fd == -1){
            perror("ERROR: accept socket incomming connection\n");
            continue;
        }
	    
        // si no hubo error al crear el socket para el cliente, se procede a crear un hilo con la funcion ThreadWork para que el usuario se conecte
        struct User newClient;

        //Almacenamiento ip
        char ip[INET_ADDRSTRLEN];
        newClient.socketFd = new_conn_fd;
        inet_ntop(AF_INET, &(incoming_conn.sin_addr), ip, INET_ADDRSTRLEN);
        newClient.ip = std::string(ip);

        pthread_t thread_id;
        pthread_attr_t attrs;
        pthread_attr_init(&attrs);
        pthread_create(&thread_id, &attrs, ThreadWork, (void *)&newClient);
    }
	
    // si hubo error al crear el socket para el cliente
    google::protobuf::ShutdownProtobufLibrary();
	return 0;
}
