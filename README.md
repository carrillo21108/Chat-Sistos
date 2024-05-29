# Chat-Sistos
💻 Chat en C++
## Curso
Sistemas Operativos
## Tecnologías utilizadas
- C++ y Makefile
- protobuf
## Comandos
Instalar los requerimientos del proyecto
```sh
sudo apt-get install protobuf-compiler libprotobuf-dev g++ make
```
Compilación
```sh
make
```
Ejecución del servidor
```sh
./server <puerto>
```
Ejecución del cliente
```sh
./client <username> <ip_server> <puerto_server>
```
## Instancia en la nube
Para la ejecución de un cliente que requiera conectarse con el servidor desplegado en la instancia de AWS, realice lo siguiente
```sh
./client <username> 3.17.14.210 8000
```
## Consideraciones de uso
- El server está configurado para asignar automáticamente el estado OFFLINE a usuarios con tiempo de inactividad mayor a 1 minuto. El usuario puede regresar a un estado ONLINE si realiza cualquier otra acción (a excepción de una asignación explícita de estados BUSY o OFFLINE por parte de este).
- Los mensajes privados únicamente pueden realizarse entre dos usuarios conectados, distintos y con un estado diferente de OFFLINE.
- El broadcast es un mensaje enviado a todos los usuarios conectados (excepto el sender), con un estado diferente de OFFLINE.
- Es posible desplegar información, únicamente de usuarios conectados.
