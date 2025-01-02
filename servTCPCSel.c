#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <sqlite3.h>
#include <ctype.h>

#define PORT 2728
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 100

// Global variables
int clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
sqlite3 *db;

// Function to check SQLite errors
void check_sqlite(int rc, const char *message)
{
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Error %s: %s\n", message, sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
}

// Initialize database and create tables
void initialize_database()
{
    int rc = sqlite3_open("trafic.db", &db);
    check_sqlite(rc, "opening database");

    const char *sql_create_tables =
        "CREATE TABLE IF NOT EXISTS Drumuri ("
        "  Name TEXT PRIMARY KEY, "
        "  Type TEXT, "
        "  Neighbours TEXT, "
        "  IntersectionPointNeighbour INTEGER, "
        "  TotalKms INTEGER, "
        "  GasStation TEXT, "
        "  Crashes TEXT, "
        "  Weather TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS Clienti ("
        "  clientid INTEGER PRIMARY KEY, "
        "  NameRoad TEXT, "
        "  LocationKm INTEGER,"
        "  Direction INTEGER "
        ");";

    rc = sqlite3_exec(db, sql_create_tables, 0, 0, 0);
    check_sqlite(rc, "creating tables");

    printf("Database initialized successfully.\n");
}

// Function to process client commands and interact with the database
void process_client_message(const char *message, char *response)
{
    const char *insert_sql = "INSERT INTO Drumuri (Name, Type, Neighbours, TotalKms, GasStation) VALUES (?, ?, ?, ?, ?)";
    const char *select_sql = "SELECT * FROM Drumuri";
    sqlite3_stmt *stmt;

    if (strstr(message, "insert_drum") != NULL)
    {
        // Inserare
        int rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, 0);
        if (rc == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, "Drum Test", -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, "Autostrada", -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, "Vecin1, Vecin2", -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 4, 300);
            sqlite3_bind_text(stmt, 5, "OMV", -1, SQLITE_STATIC);

            if (sqlite3_step(stmt) == SQLITE_DONE)
            {
                printf("Drum adăugat cu succes.\n");
            }
            else
            {
                printf("Eroare la adăugare: %s\n", sqlite3_errmsg(db));
            }
        }
        sqlite3_finalize(stmt); // Finalizezi după fiecare statement

        // Afișare
        rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, 0);
        if (rc == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                const char *nume = (const char *)sqlite3_column_text(stmt, 0);
                const char *tip = (const char *)sqlite3_column_text(stmt, 1);
                printf("Drum: %s, Tip: %s\n", nume, tip);
            }
        }
        sqlite3_finalize(stmt); // Finalizezi după utilizarea statement-ului
        snprintf(response, BUFFER_SIZE, "Drum adăugat cu succes.\n");
    }
    else if (strstr(message, "exit") != NULL)
    {
        snprintf(response, BUFFER_SIZE, "Server is closing the connection.\n");
    }
    else
    {
        snprintf(response, BUFFER_SIZE, "Unknown command.\n");
    }
}

// Handle client communication
void *handle_client(void *arg)
{
    int client_socket = *(int *)arg;
    free(arg);
    char buffer[BUFFER_SIZE];

    printf("New client connected. Socket fd: %d\n", client_socket);

    while (1)
    {
        bzero(buffer, BUFFER_SIZE);
        int valread = read(client_socket, buffer, BUFFER_SIZE - 1);
        if (valread <= 0)
        {
            printf("Client disconnected. Socket fd: %d\n", client_socket);
            close(client_socket);
            break;
        }

        buffer[valread] = '\0';
        char response[BUFFER_SIZE];
        bzero(response, BUFFER_SIZE);

        process_client_message(buffer, response);
        if (write(client_socket, response, strlen(response)) <= 0)
        {
            perror("Error writing to client");
            close(client_socket);
            break;
        }

        if (strcasecmp(buffer, "exit") == 0)
        {
            close(client_socket);
            break;
        }
    }

    return NULL;
}

int main()
{
    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Initialize database
    initialize_database();

    // Initialize clients array
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i] = -1;
    }

    // Create server socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, MAX_CLIENTS) == -1)
    {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);

    while (1)
    {
        int *new_socket = malloc(sizeof(int));
        *new_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (*new_socket < 0)
        {
            perror("Accept failed");
            free(new_socket);
            continue;
        }

        printf("New connection: IP %s, Port %d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, new_socket);
        pthread_detach(thread_id);
    }

    sqlite3_close(db);
    close(server_socket);
    return 0;
}
