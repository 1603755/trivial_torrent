
// Trivial Torrent

// TODO: some includes here

#include "logger.h"
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h> 
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <stdlib.h>
#include "file_io.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>



// TODO: hey!? what is this?

/** 
 * This is the magic number (already stored in network byte order).
 * See https://en.wikipedia.org/wiki/Magic_number_(programming)#In_protocols
 */
static const uint32_t MAGIC_NUMBER = 0xde1c3232; // = htonl(0x32321cde);

static const uint8_t MSG_REQUEST = 0;
static const uint8_t MSG_RESPONSE_OK = 1;
static const uint8_t MSG_RESPONSE_NA = 2;

enum { RAW_MESSAGE_SIZE = 13 };
int client (char **argv);
int server (char **argv);

/**
 * Main function.
 */
int main (int argc, char **argv) {
	set_log_level(LOG_DEBUG);

	log_printf(LOG_INFO, "Trivial Torrent (build %s %s) by %s", __DATE__, __TIME__, "Arnau Fornaguera Orpinell(1603755) and J.DOE");
	
	if (argc == 2) {
		int ctl_client = client(argv);
		if(ctl_client == -1) {
			perror("Oooops something happened with client");
			exit(-1);
		}
	}
	else if (argc == 4) {
		int ctl_server = server(argv);
		if(ctl_server == -1){
			perror("Oooops something happened with server");
			exit(-1);
		}
		
	} else {
		perror("Commands error");
		exit(-1);
	}
	exit(0); //Program ends correctly :) 
}

int client(char **argv){

	//Assignem el nom de la metainfo que passem per comandes a una array de char per després carregar la METAINFO
	
	struct torrent_t torrent;
	char* downloaded_file_name = argv[1];
	
	//Fem la comprobació de que l'arxiu existeix
	
	int ctl_access = access((const char *)downloaded_file_name, F_OK);
	if (ctl_access != 0) { 	//Comprovem que existeixi l'arxiu en la seva ubicació
		perror("L'arxiu especificat no existeix."); 
		return -1;
	}
	//En cas de que existeixi crearem la metainfo associada
	
	//Creem un buffer amb la ubicació on descarragarem la metainfo
	size_t count = strlen(downloaded_file_name);
	assert(count > 9);
	
	char *var = ".ttorrent";
	for(size_t i = count-9; i < count; i++){
		assert(var[i-count+9] == downloaded_file_name[i]);
	}
	
	char file[count-9];
	for(size_t i = 0; i < count - 9; i++){
		file[i] = downloaded_file_name[i];
	}
	//char file[41] = "torrent_samples/client/test_file";

	int ctl_file = create_torrent_from_metainfo_file(argv[1], (struct torrent_t*) &torrent, (const char *) file);
	if (ctl_file ==  -1) {
		perror("L'arxiu no s'ha pogut crear correctament");
		return -1;
	}
	
	//Ens anem conectant a cada peer per sol·licitar els blocks que tenim incorrectes
	
	for (uint64_t i = 0; i < torrent.peer_count; i++) {
		
		//Creem el socket corresponent al client
		
		int ctl_s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
		if (ctl_s == -1) {
			perror("S'ha creat el socket incorrectament");
			return -1;
		}
		
		//Inicialitzem les adreces per cada peer
		
		struct sockaddr_in con_addr;
		con_addr.sin_family = AF_INET;
		
		con_addr.sin_port = torrent.peers[i].peer_port;
		con_addr.sin_addr.s_addr = 0;
		
		//Carreguem la adreça de cada port
		
		for (uint64_t j = 0; j < 4; j++) {
			uint8_t addr = torrent.peers[i].peer_address[j];
			uint32_t aux = 0;
			con_addr.sin_addr.s_addr = con_addr.sin_addr.s_addr | (aux | addr) << (8 * j); 
		}
		
		//Ens conectem amb el servidor
		
		int ctl_connect = connect(ctl_s, (struct sockaddr *) &con_addr, sizeof(struct sockaddr));
		if (ctl_connect == -1) {
			close(ctl_s);
			perror("Cannot establish connection with server");
			continue;
		}
		
		//Per cada bloc que tenim 
		
		for (uint64_t j = 0; j < torrent.block_count; j++) {
		
			//En cas de que un bloc no sigui correcte el sol·licitem al servidor
			
			if (!torrent.block_map[j]) {
			
				//Buffer per crear la nostra sol·licitud que enviarem al servidor
				 
				uint8_t payload[RAW_MESSAGE_SIZE];
				
				//Carreguem el numero màgic al buffer
				
				for (uint64_t v = 0; v < 4; v++) {
					payload[v] = (uint8_t) (MAGIC_NUMBER >> 8 * (3 - v));
				}
				
				//Posem el tipus de missatge (Request)
				
				payload[4] = MSG_REQUEST;
				
				//Carreguem el numero del bloc al payload
				
				for ( int v = 5; v < 13; v++ ) {
					payload[v] = (uint8_t) (j >> 8 * (12 - v));
				}
				
				//Enviem el payload al Servidor
				
				ssize_t msg_payload = send(ctl_s, (uint8_t *) payload, RAW_MESSAGE_SIZE, 0);
				if (msg_payload == -1) {
					perror("Cannot send Payload correctly to server");
					continue;
				}
				
				//Creem la estructura del bloc per rebre la resposta del servidor
				struct block_t block_recieved;
				
				//Dins el nostre buffer que guardem el missatge rebut hi han 13 bytes destinats a la resposta del nostre payload NUM_MAGIC, MESSAGE_TYPE i CODI_BLOCK
				
				uint8_t data[MAX_BLOCK_SIZE + 13];
				
				ssize_t ctl_rcv = 0;
				
				//Depenent de si el bloc és el últim o no fem un recieve amb MSG_WAITALL o argument 0
				
				if (j != torrent.block_count - 1) {
					ctl_rcv = recv(ctl_s, (uint8_t *)&data, MAX_BLOCK_SIZE+13, MSG_WAITALL);
					if (ctl_rcv == -1) {
						perror("Cannot recieve the block correctly");
						continue;
					}
				} else {
					ctl_rcv = recv(ctl_s, (uint8_t *)&data, MAX_BLOCK_SIZE+13, 0);
					if (ctl_rcv == -1) {
						perror("Cannot recieve the block correctly");
						continue;
					}
				}
				
				assert( ctl_rcv >= 0 && ctl_rcv <= MAX_BLOCK_SIZE+13);
				
				//Guardem el bloc sense el header on descarreguem els blocs
				
				for (int x = 0; x < ctl_rcv; x++) {
					block_recieved.data[x] = data[x + 13];
				}
				
				//Guardem el tamany de la data en el block menys 13 ja que la capçalera no l'hem de guardar
				assert(ctl_rcv >= 13);
				block_recieved.size = (uint64_t) (ctl_rcv - 13);
				
				//Fem el store block en el .ttorrent i en cas de no ser correcte fem un continue
				assert(block_recieved.size != 0);
				int str_blk = store_block ((struct torrent_t*) &torrent, j, (struct block_t*) &block_recieved);
				if (str_blk == -1) {
					perror("Cannot store block correctly to metainfo");
					continue;
				}
			
				//En cas de no tenir el bloc continuem
				
			}
			
		}
		
		//Tanquem el socket per cada peer ja que així finalitzem la connexió amb el servidor i podem tornar-nos a conectar a la mateix a direcció un altre cop
		ssize_t ctl_close = close(ctl_s);
		if (ctl_close == -1) {
			perror("Cannot close correctly the socket");
			continue;
		}
	
	}
	free(torrent.block_map);
	free(torrent.peers);
	free(torrent.block_hashes);
	
	return 0;
	
}

int server(char **argv) {

	//Convertim el port passar per parametre en char a int (8080)
	
	uint16_t PORT = 0;
	uint16_t PORT_NUMBER[(int) sizeof(argv[2])];
	
	for (uint16_t i = 0; i < sizeof(argv[2]); i++) {
		PORT_NUMBER[i] = (uint16_t) (argv[2][i] - 48);
	}
	
	//Carreguem el bloc corresponent
	
	uint16_t aux = 1;
	for (int i = (int) strlen(argv[2]) - 1; i >= 0; i--) {
		PORT += (uint16_t)(PORT_NUMBER[i]*aux);
		aux *=  10;

	}
	
	//Creem l'estructura del torrent
	
	struct torrent_t torrent;
	char* downloaded_file_name = argv[3];
	
	//Fem la comprobació de que l'arxiu existeix

	int ctl_access = access((const char *) downloaded_file_name, F_OK);
	if (ctl_access != 0) { 	//Comprovem que existeixi l'arxiu en la seva ubicació
		perror("L'arxiu especificat no existeix."); 
		return -1;
	}
	
	
	//Creem un buffer amb la ubicació on descarragarem la metainfo
	size_t count = strlen(downloaded_file_name);
	assert(count > 0);
	
	char *var = ".ttorrent";
	for(size_t i = count-9; i < count; i++){
		assert(var[i-count+9] == downloaded_file_name[i]);
	}
	
	char file[count-9];
	for(size_t i = 0; i < count - 9; i++){
		file[i] = downloaded_file_name[i];
	}
	
	//Creem l'arxiu de la metainfo en cas de que aquest sigui correct i sinó retornem -1	
	int ctl_file = create_torrent_from_metainfo_file(downloaded_file_name, (struct torrent_t*) &torrent, (const char *) file);
	if (ctl_file ==  -1) {
		perror("L'arxiu no s'ha pogut crear correctament");
		return -1;
	}
	
	//Fem la creació del mean socket
	
	int ctl_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (ctl_sock == -1) {
		perror("Cannot create socket correctly");
		return -1;
	}
	
	
	//Creem la estructura de l'adreça del socket
	
	struct sockaddr_in con_addr;
	con_addr.sin_family = AF_INET;
	con_addr.sin_port = htons(PORT);
	
	//Posem que el servidor apunta a qualsevol adreça
	
	con_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	//Fem el bind()
	
	int ctl_bind = bind(ctl_sock, (struct sockaddr*) &con_addr, sizeof(con_addr));
	if (ctl_bind ==  -1) {
		perror("Cannot do bind correctly");
		return -1;
	}
	
	//Fem el listen y posem que el tamany de la llista d'espera sigui la màxima (SOMAXCONN)
	
	int ctl_listen = listen(ctl_sock, SOMAXCONN);
	if (ctl_listen ==  -1) {
		perror("L'arxiu no s'ha pogut crear correctament");
		return -1;
	}
	
	//Per sempre el servidor estarà escoltant les noves peticions del client 
	
	while (1) {
	
		// Creem el nou socket dedicat a les peticions del client i anem acceptant una a una les peticions
		
		socklen_t tcpHost = sizeof(con_addr);
		int ctl_new_sock = accept(ctl_sock, (struct sockaddr *) &con_addr, &tcpHost);
		if (ctl_new_sock ==  -1) {
			exit(EXIT_SUCCESS);
			perror("Cannot create a socket correctly");
			continue;
		}
		
		//Procés fill
		
		int child = 0;
		if ((child = fork()) == 0) { 
			
			//Tanquem el socket anterior en el nou procés(fill) per no tenir dos file descriptors en un mateix procés
			
			int ctl_close = close(ctl_sock);
			if (ctl_close == -1) {
				perror("Problems closing main socket");
			}
			
			//Rebem el missatge del fent un reccieve, així obtenim el payload
			
			int error = 0;
			uint8_t payload[13];
			ssize_t ctl_recv = 0;
			while ((ctl_recv = recv(ctl_new_sock, payload, RAW_MESSAGE_SIZE, MSG_WAITALL)) > 0) { 
			
				if (ctl_recv == -1) {
					perror("Server didn't recieve the message correctly");
					error = 1;
				}
				
				//Passem el numero el qual està fraccionat en uint8_t a uint64_t 
				
				struct block_t retBlock;
				uint64_t block_num = 0;
				for (int i = 5; i < RAW_MESSAGE_SIZE; i++) {
					block_num |= (block_num << 8);
					block_num |= payload[i];
				}
				assert(block_num < MAX_BLOCK_SIZE);
				//Carreguem el block que ens ha passat el client en el payload al nostre estructura de block
				
				int ctl_load_block = load_block((struct torrent_t *) &torrent, block_num, (struct block_t *) &retBlock);
				if (ctl_load_block == -1) {
					perror("We cannot load block correctly");
					error = 1;
				}
				
				//Depenent de si trobem el block solicitat respondrem retornant el payload(en cas de que no es trobi o estigui malament) o retornarem el payload amb el bloc
	
				if (!error) { 
					
					//Preparem el missatge de retorn
					
					uint8_t returnMessage[retBlock.size+13];
					for (uint64_t i = 0; i < retBlock.size+13; i++) {
						if (i < 13) {
						
							if (i != 4) {
								returnMessage[i] = payload[i];
							} else {
								returnMessage[i] = MSG_RESPONSE_OK; 
							}
							
						} else {
							returnMessage[i] = retBlock.data[i-13];
						}
						
					}
					
					//Quan tenim el missatge preparat el retornem
					
					ssize_t ctl_send = send(ctl_new_sock, (uint8_t *) returnMessage, sizeof(returnMessage), 0); 
					if (ctl_send == -1) {
						perror("Error sending message response");
						continue;
					}
					
				} else { //Response with MSG_RESPONSE_NA
					
					//En cas de que el bloc sigui incorrecte, llavors retornem el payload dient que no s'ha trobat el bloc
					payload[4] = MSG_RESPONSE_NA;
					ssize_t ctl_send = send(ctl_new_sock, (uint8_t *) payload, sizeof(payload), 0); //MSG_CONFIRM??
					if (ctl_send == -1) {
						perror("Error sending message response");
						continue;
					}
				}
			}
			free(torrent.block_map);
			free(torrent.peers);
			free(torrent.block_hashes);
			//Tanquem el nou socket generat per atendre el client
			int ctl_close_new = close(ctl_new_sock);
			if (ctl_close_new == -1) {
				perror("Problems closing main socket");
				continue;
			}
			
		} else { 
		
			//Procés Pare
			//Esperem a que el fill acabi
			
			waitpid(-1, &child, 0);
			//Tanquem el nou socket generat per atendre el client
			int ctl_close_new = close(ctl_new_sock);
			if (ctl_close_new == -1) {
				perror("Problems closing main socket");
				continue;
			}
		
		}
		
	}
	free(torrent.block_map);
	free(torrent.peers);
	free(torrent.block_hashes);
	
	//Tanquem el main socket
	int ctl_close = close(ctl_sock);
	if (ctl_close == -1) {
		perror("Problems closing main socket");
		return -1;
	}
	
	return 0;
	
}

