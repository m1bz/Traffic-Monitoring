#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 1024

extern int errno;

int port;
int sd; // socket descriptor global pentru a fi accesibil și în thread
int running = 1; // variabilă globală pentru a semnaliza thread-ului de citire când să se oprească

void *read_thread(void *arg) {
    char msg[BUFFER_SIZE];
    while (running) {
        bzero(msg, BUFFER_SIZE);
        int bytes_read = read(sd, msg, BUFFER_SIZE - 1);
        if (bytes_read < 0) {
            perror("[client] Error at read() from server in thread.\n");
            running = 0;
            break;
        } else if (bytes_read == 0) {
            // Server closed connection
            printf("Server closed the connection.\n");
            running = 0;
            break;
        }

        msg[bytes_read] = '\0';

        // Afișăm direct orice mesaj primit
        printf("\n%s", msg);

        // Dacă serverul semnalează închidere
        if (strstr(msg, "Server is closing the connection.\n") != NULL) {
            running = 0;
            break;
        }

        // Prompt pentru următoarea comandă
        printf("\nType next command: \n");
        fflush(stdout);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server;
    char msg[BUFFER_SIZE];

    if (argc != 3) {
        printf("[client] Syntax: %s <server_address> <port>\n", argv[0]);
        return -1;
    }

    port = atoi(argv[2]);

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("[client] Error at socket().\n");
        return errno;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(argv[1]);
    server.sin_port = htons(port);

    if (connect(sd, (struct sockaddr*)&server, sizeof(struct sockaddr)) == -1) {
        perror("[client] Error at connect().\n");
        return errno;
    }

    printf("Bun venit! Ați intrat în sistemul Wish GPS. Pentru a afla comenzile disponibile, tastați help.\n");
    printf("Pentru a va localiza va rog sa scrieti locatie urmat de numele drumului, numărul markerului de drum pe care l-ați depășit cel mai recent si viteza curenta pe care o aveti:\n");

    // Creăm thread-ul pentru citire asincronă de la server
    pthread_t tid;
    if (pthread_create(&tid, NULL, read_thread, NULL) != 0) {
        perror("[client] Error creating read thread.\n");
        close(sd);
        return errno;
    }

    while (running) {
        // Citim comenzi de la stdin
        bzero(msg, BUFFER_SIZE);
        fflush(stdout);
        if (read(0, msg, BUFFER_SIZE - 1) < 0) {
            perror("[client] Error reading from stdin.\n");
            break;
        }

        // Asigurăm că se termină cu '\0'
        msg[strlen(msg)] = '\0';

        // Eliminăm newline-urile manual dacă există
        char *newline = strchr(msg, '\n');
        if (newline) *newline = '\0';
        newline = strchr(msg, '\r');
        if (newline) *newline = '\0';

        // Verificăm dacă utilizatorul vrea să iasă
        if (strncasecmp(msg, "exit", 4) == 0) {
            printf("[client] Exiting...\n");
            if (write(sd, msg, strlen(msg)) <= 0) {
                perror("[client] Error at write() to server.\n");
            }
            running = 0;
            break;
        }

        // Trimitem comanda la server
        if (write(sd, msg, strlen(msg)) <= 0) {
            perror("[client] Error at write() to server.\n");
            running = 0;
            break;
        }

        // Afișăm prompt provizoriu (răspunsul va veni pe thread-ul de citire)
        printf("Comanda trimisă. Aștept răspuns...\n");
    }

    close(sd);
    running = 0; 
    pthread_join(tid, NULL);

    return 0;
}
