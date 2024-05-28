#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include "chat.pb.h"
#include <iostream>

using namespace std;

// Inicializar variables globales
int connected, waitingForServerResponse;
#define length 8192

// ----------------------------------------- Funciones del controlador, enviar y recibir info del server -----------------------------------------
// Funcion para obtener el ip de un usuario 
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET){
		return &(((struct sockaddr_in *)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

// Event listener de respuestas del server
void *listenToMessages(void *args)
{	
	// Siempre esta escuchando y espera por una respuesta
	while (1){
		
		// Inicializamos las variabless
		std::vector<char> buffer_msg(length);
		int *sockmsg = (int *)args;

		// Obtencion de respuesta
		chat::Response *response = new chat::Response();
		ssize_t bytesRead = recv(*sockmsg, buffer_msg.data(), buffer_msg.size(), 0);
		response->ParseFromArray(buffer_msg.data(),bytesRead);
		
		// En caso el estado de la respuesta es ERROR
		if (response->status_code() != chat::OK){
			cout <<response->message()<<endl;
			
		} else {
			// Para recibir mensajes, segun el protocolo, se utiliza el server response y el server message
			switch (response->operation())
			{
			// Respuestas de tipo mensaje entrante
			case chat::INCOMING_MESSAGE:{
				std::cout<<response->message();
				std::cout<<response->incoming_message().content()<<std::endl;
				break;
			}
			// Respuestas de tipo envio de mensajes
			case chat::SEND_MESSAGE:{
				std::cout<<response->message();
				break;
			}
			// Respuestas de tipo obtencion de informacion
			case chat::GET_USERS:{
				std::cout<<response->message();
				for(int i = 0; i<response->user_list().users_size(); i++){
					std::cout << "\nUsername: " << response->user_list().users(i).username()<< std::endl;
					switch(response->user_list().users(i).status()){
						case chat::ONLINE:{
							std::cout << "State: ONLINE" << std::endl;
							break;
						}
						case chat::BUSY:{
							std::cout << "State: BUSY" << std::endl;
							break;
						}
						case chat::OFFLINE:{
							std::cout << "State: OFFLINE" << std::endl;
							break;
						}
					}
					
				}
				break;
			}
			// Respuesta de tipo Actualizacion de estado
			case chat::UPDATE_STATUS:{
				std::cout<<response->message()<<std::endl;
				break;
			}
			default:
				break;
			}
		}
		// Si se desconecta del server 
		response->Clear();
		waitingForServerResponse = 0;
		if (connected == 0){
			pthread_exit(0);
		}
	}
}


// ----------------------------------------- inicio main del cliente -----------------------------------------
// main con opciones para que ingrese el usuario
int main(int argc, char const* argv[])
{
	
	// ----------------------------------------- funciones y variables para el controlador del cliente -----------------------------------------
	// variables para enviar info al server
	int sockfd, numbytes;
	// Buffer para datos
	char buf[length];
	// hints: pautas para getaddrinfo
	// servinfo: lista de direcciones de servidor
	// p: puntero para iteraar sobre las direcciones
	struct addrinfo hints, *servinfo, *p;
	int rv;
	// bufer para almacenar la IP
	char s[INET6_ADDRSTRLEN];
	
	// si no se ingresan los params necesarios
	if (argc != 4){
		fprintf(stderr, "Use: client <username> <server ip> <server port>\n");
		exit(1);
	}
	
	// Variables para configurar el server 
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	
	// si no se recibe la informacion del servidor hay error
	if ((rv = getaddrinfo(argv[2], argv[3], &hints, &servinfo)) != 0){
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}
	
	// buscar en los sockets si hay errores
	for (p = servinfo; p != NULL; p = p->ai_next)
	{
		// aqui se encuentra si hay error al crear el socket
		if ((sockfd = socket(p->ai_family, p->ai_socktype,p->ai_protocol)) == -1) {
			perror("ERROR: socket");
			continue;
		}
		
		// aqui se encuentra si hay error al conectarse al servidor
        	if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("ERROR: connect client");
			close(sockfd);
			continue;
		}
		break;
	}
	
	// No se encontro un socket
	if (p == NULL) {
		fprintf(stderr, "ERROR: failed to connect\n");
		return 2;
	}
	
	// Se encontro el socket
	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),s, sizeof s);
	printf("CONNECTED IP: %s\n", s);
	freeaddrinfo(servinfo);

	// Definicion de estructuras
    chat::Request *request = new chat::Request();
	chat::NewUserRequest *newUser = request->mutable_register_user();

	chat::Response *response = new chat::Response();

	// Message register
	std::vector<char> buffer(length);
	request->set_operation(chat::REGISTER_USER);
	newUser->set_username(argv[1]);
	//request->set_allocated_register_user(newUser);

    // Serializar la solicitud a una cadena
    std::string serialized_request;
    if (!request->SerializeToString(&serialized_request)) {
        std::cerr << "ERROR: failed to serialized" << std::endl;
        return 1;
    }

	// Copiando mensaje en buffer
	send(sockfd, serialized_request.data(), serialized_request.size(), 0);
	ssize_t bytesRead = recv(sockfd, buffer.data(), buffer.size(), 0);
	response->ParseFromArray(buffer.data(),bytesRead);

	// Si hubo error al buscar 
	if (response->status_code() != chat::OK) {
		std::cout << response->message()<< std::endl;
		return 0;
	}

	// Si no hay errores, entonces se puede abrir un fork y hostear al cliente
	std::cout << "SERVER: "<< response->message()<< std::endl;	
	connected = 1;

	pthread_t thread_id;
	pthread_attr_t attrs;
	pthread_attr_init(&attrs);
	pthread_create(&thread_id, &attrs, listenToMessages, (void *)&sockfd);

	int proceed = 1;
	char client_opt;
	
    // Esperar a que el cliente se conecte correctamente al servidor y abrir el controlador

	// ----------------------------------------- Inicio controlador del cliente -----------------------------------------
    while (proceed){
	
        while (waitingForServerResponse == 1) {}

		printf("\n1 -> Chat with everyone in the chat (Broadcasting)\n2 -> Send a private message\n3 -> Change status\n4 -> List connected users in the chat system\n5 -> Deploy info from a particular user\n6 -> Exit\nEnter the option: ");
        request->Clear();

		//Ingreso de valor numerico por el cliente
        cin>>client_opt;
	
		// Para ingresar las opciones en cliente
        switch (client_opt){
		
		// Broadcast
		case '1':{
				std::string message;
				cout<<"Enter the message to be sent: ";
				std::cin.ignore(); // Ignora el caracter de nueva linea dejado por 'cin >>' anterior
    			std::getline(std::cin, message);

				chat::SendMessageRequest *newMessage = request->mutable_send_message();

				request->set_operation(chat::SEND_MESSAGE);
				newMessage->set_content(message);
				//request->set_allocated_send_message(newMessage);
				request->SerializeToString(&serialized_request);

				send(sockfd, serialized_request.data(), serialized_request.size(), 0);
				waitingForServerResponse = 1;
				break;
		}
			
		// Mensaje privado
		case '2':{
				std::string recipient, message;
				cout<<"Enter username of the recipient: ";
				std::cin.ignore();
    			std::getline(std::cin, recipient);

				cout<<"Enter the message to be sent: ";
    			std::getline(std::cin, message);

				chat::SendMessageRequest *newMessage = request->mutable_send_message();
				request->set_operation(chat::SEND_MESSAGE);
				newMessage->set_recipient(recipient);
				newMessage->set_content(message);
				//request->set_allocated_send_message(newMessage);
				request->SerializeToString(&serialized_request);

				send(sockfd, serialized_request.data(), serialized_request.size(), 0);
				waitingForServerResponse = 1;
				break;
		}
			
		// Cambiar status
		case '3':{
				std::string op;
				chat::UpdateStatusRequest *newStatus = request->mutable_update_status();
				cout<<"Select between these options\n1 -> ONLINE\n2 -> BUSY\n3 -> OFFLINE\nEnter the new status: ";
				
				std::cin.ignore();
				cin>>op;

				if (op=="1"||op=="ONLINE"||op=="oline"){
					newStatus->set_new_status(chat::ONLINE);
				}
				else if (op=="2"||op=="BUSY"||op=="busy"){
					newStatus->set_new_status(chat::BUSY);
				}
				else if (op=="3"||op=="OFFLINE"||op=="offline"){
					newStatus->set_new_status(chat::OFFLINE);
				}
				else{
					printf("The value entered is invalid\n");
					break;
				}

				newStatus->set_username(argv[1]);

				request->set_operation(chat::UPDATE_STATUS);
				//request->set_allocated_update_status(newStatus);
				request->SerializeToString(&serialized_request);

				send(sockfd, serialized_request.data(), serialized_request.size(), 0);
				waitingForServerResponse = 1;
				break;
		}
			
		// Mostrar a todos los usuarios conectados
            	case '4':{
				chat::UserListRequest *list = request->mutable_get_users();

				request->set_operation(chat::GET_USERS);
				//request->set_allocated_get_users(list);
				request->SerializeToString(&serialized_request);

				send(sockfd, serialized_request.data(), serialized_request.size(), 0);
				waitingForServerResponse = 1;
                break;
		}
		
		// Informacion de usuarios
		case '5':{
				std::string username;
				chat::UserListRequest *info = request->mutable_get_users();

				cout<<"Enter the username: ";
				std::cin.ignore();
    			std::getline(std::cin, username);

				info->set_username(username);

				request->set_operation(chat::GET_USERS);
				//request->set_allocated_get_users(info);
				request->SerializeToString(&serialized_request);

				send(sockfd, serialized_request.data(), serialized_request.size(), 0);
				waitingForServerResponse = 1;
                break;
		}
		
		// Cerrar sesion
		case '6':{
				std::string username;
				chat::User *userI = request->mutable_unregister_user();

				userI->set_username(argv[1]);
				userI->set_status(chat::OFFLINE);

				request->set_operation(chat::UNREGISTER_USER);
				//request->set_allocated_unregister_user(userI);
				request->SerializeToString(&serialized_request);
				proceed = 0;
                break;
        }
			
		default:{
				cout<<"Option invalid."<<std::endl;
				break;
		}

        }
    }

	// Al cerrar sesion, se destruye el hilo
	pthread_cancel(thread_id);
	printf("Thanks for using this server chat %s\n!",argv[1]);
	return 0;
}
