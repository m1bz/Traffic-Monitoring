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
char  weather[5][10] = {"sunny", "rainy", "foggy", "snowy", "windy"};
char  road_type[3][10] = {"autostrada", "drum", "oras"};
int clients[MAX_CLIENTS];
int client_count = 0;

typedef struct {
    char *name;
    int km,nrneighbours;
}Neighbour;

typedef struct{
    char *neighbours;
    char *intersectionpointneighbours;
}Formation_neighbour;

pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t db_lock = PTHREAD_MUTEX_INITIALIZER;

sqlite3 *db;

// Function to lock/unlock DB
void lock_db() {
    pthread_mutex_lock(&db_lock);
}

void unlock_db() {
    pthread_mutex_unlock(&db_lock);
}

void check_sqlite(int rc, const char *message);
void initialize_database();
char *get_nth_word(const char *input, int n);
char *check_bd_insert(const char *message, char *response);
char *insert_bd(const char *message, char *response);
int generatekm(char *message);
Neighbour get_avaible_neighbour_db();
char *check_bd_neighbour();
int min(int a, int b);
Formation_neighbour Formate(Formation_neighbour fn);

void process_client_message(const char *message, char *response)
{
    if (strstr(message, "locatie") != NULL)
    {
        check_bd_insert(message, response);
    }
    else if (strstr(message, "insert_drum") != NULL)
    {
        insert_bd(message, response);
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

char *get_nth_word(const char *input, int n)
{
    if (input == NULL || n <= 0)
    {
        return NULL;
    }

    char *input_copy = strdup(input);
    if (input_copy == NULL)
    {
        perror("Allocation failed");
        exit(EXIT_FAILURE);
    }

    char *token = strtok(input_copy, " ");
    int word_count = 1;

    while (token != NULL)
    {
        if (word_count == n)
        {
            char *nth_word = strdup(token);
            free(input_copy);
            return nth_word;
        }
        token = strtok(NULL, " ");
        word_count++;
    }

    free(input_copy);
    return NULL;
}

void initialize_database()
{
    // Deschidem baza de date
    int rc = sqlite3_open("trafic.db", &db);
    check_sqlite(rc, "opening database");

    // Cream tabelele
    const char *sql_create_tables =
        "CREATE TABLE IF NOT EXISTS Drumuri ("
        "  Name TEXT PRIMARY KEY, "
        "  Type TEXT, "
        "  Neighbours TEXT, "    // despartiti prin space
        "  IntersectionPointNeighbour TEXT, " // despartit tot prin space, ar trebui sa fie ordonat crescator, deci si neighbour ar trebui sa fie ordonat
        "  NeighbourNumber INTEGER, "
        "  TotalKms INTEGER, "
        "  GasStation INTEGER, "
        "  Crashes INTEGER, "
        "  Weather TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS Clienti ("
        "  clientid INTEGER PRIMARY KEY AUTOINCREMENT, "
        "  NameRoad TEXT, "
        "  LocationKm INTEGER,"
        "  Direction INTEGER "
        ");";

    rc = sqlite3_exec(db, sql_create_tables, 0, 0, NULL);
    check_sqlite(rc, "creating tables");

    printf("Database initialized successfully.\n");
}

char *check_bd_insert(const char *message, char *response)
{
    lock_db();  // Închidem lacătul pe BD
    sqlite3_stmt *stmt;
    const char *select_sql = "SELECT Name FROM Drumuri";

    int rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        snprintf(response, BUFFER_SIZE, "SQL error preparing statement: %s\n", sqlite3_errmsg(db));
        unlock_db();
        return response;
    }

    // Obținem cuvântul 2 din message (numele drumului pe care-l căutăm)
    char *road_name = get_nth_word(message, 2);
    if (!road_name)
    {
        snprintf(response, BUFFER_SIZE, "Nu ai trimis un nume de drum.\n");
        sqlite3_finalize(stmt);
        unlock_db();
        return response;
    }

    int check = 0;
    // Parcurgem rezultatele
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char *nume = (const char *)sqlite3_column_text(stmt, 0);
        // Comparam corect conținutul
        if (strcmp(nume, road_name) == 0)
        {
            check = 1;
            break;
        }
    }
    sqlite3_finalize(stmt);
    free(road_name); // eliberăm memorie, get_nth_word returnează strdup

    if (check == 1)
    {
        snprintf(response, BUFFER_SIZE, "V-am gasit locatia!\n");
    }
    else
    {
        snprintf(response, BUFFER_SIZE,
                 "Nu v-am gasit locatia in baza noastra de date. "
                 "Va rog inserati astfel: insert_drum <nume_drum> <tip_drum>.\n");
    }

    unlock_db();  // Eliberăm lacătul pe BD
    return response;
}

char *insert_bd(const char *message, char *response)
{
    lock_db();  // Închidem lacătul pe BD

    // Obținem parametrii 2 și 3 din mesaj
    char *road_name = get_nth_word(message, 2);
    char *road_type = get_nth_word(message, 3);

    if (!road_name || !road_type)
    {
        snprintf(response, BUFFER_SIZE, "Parametri insuficienti. Folositi: insert_drum <nume> <tip>.\n");
        if (road_name) free(road_name);
        if (road_type) free(road_type);
        unlock_db();
        return response;
    }

    const char *insert_sql = 
        "INSERT INTO Drumuri (Name, Type, Neighbours, IntersectionPointNeighbour, NeighbourNumber, TotalKms, GasStation, Crashes, Weather) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    const char * update_sql = 
        "UPDATE Drumuri SET Neighbours = ?, IntersectionPointNeighbour = ?, NeighbourNumber = ? WHERE Name = ?";
        "VALUES (?, ?, ?, ?);";

    sqlite3_stmt *stmt;
    sqlite3_stmt *stmt2;
    int nc = sqlite3_prepare_v2(db, update_sql, -1, &stmt2, NULL);
    int rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK && nc != SQLITE_OK)
    {
        snprintf(response, BUFFER_SIZE, "SQL error preparing statement: %s\n", sqlite3_errmsg(db));
        free(road_name);
        free(road_type);
        unlock_db();
        return response;
    }
    int km = generatekm(road_type), neighborkm;
    Neighbour n = get_avaible_neighbour_db();
    // Legăm parametrii folosind SQLITE_TRANSIENT, astfel SQLite își face singur copie
    sqlite3_bind_text(stmt, 1, road_name, -1, SQLITE_TRANSIENT); // nume
    sqlite3_bind_text(stmt, 2, road_type, -1, SQLITE_TRANSIENT); // tip
    sqlite3_bind_text(stmt, 3, n.name, -1, SQLITE_TRANSIENT); // vecini
    sqlite3_bind_int(stmt, 4, rand() %min(n.km,km), -1, SQLITE_TRANSIENT); // nodul intersectie cu vecini
    // am adaugat vecini pentru drumul pe care il inserez, mai trb sa adaug si pentru drumul care se va invecina acum cu el
    sqlite3_bind_int(stmt, 5, 1); // numar vecini totali
    sqlite3_bind_text(stmt, 6, km); // total km drum
    sqlite3_bind_int(stmt, 7, rand() % km); // benzinarie
    sqlite3_bind_text(stmt, 8, 0); // accidente
    sqlite3_bind_text(stmt, 9, weather[rand() % 5], -1, SQLITE_TRANSIENT); //vreme
    rc = sqlite3_step(stmt);
    sqlite3_bind_text(stmt2, 4, n.name, -1, SQLITE_TRANSIENT); // nume
    sqlite3_bind_text(stmt2, 3, n.nrneighbours+1); // numar vecini
    Formation_neighbour fn;
    Formate(fn);
    sqlite3_bind_text(stmt2, 1, fn.neighbours, -1, SQLITE_TRANSIENT); // vecini
    sqlite3_bind_text(stmt2, 2, fn.intersectionpointneighbours, -1, SQLITE_TRANSIENT); // nodul intersectie cu vecini
    nc = sqlite3_step(stmt2);
    if (rc == SQLITE_DONE && nc == SQLITE_DONE)
    {
        snprintf(response, BUFFER_SIZE, "Drum adăugat cu succes.\n");
    }
    else
    {
        snprintf(response, BUFFER_SIZE, "Eroare la adăugare: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    free(road_name);
    free(road_type);

    unlock_db(); 
    return response;
}


void check_sqlite(int rc, const char *message)
{
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Error %s: %s\n", message, sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
}

int generatekm(char *message)
{
    int km = 0;
    if (strcmp(message, "autostrada") == 0)
    {
        km = rand() % 75 + 200;
    }
    else if (strcmp(message, "drum") == 0)
    {
        km = rand() % 75 + 100;
    }
    else if (strcmp(message, "oras") == 0)
    {
        km = rand() % 50 + 75;
    }
    return km;
}


Neighbour get_avaible_neighbour_db()
{
    sqlite3_stmt *stmt;
    const char *select_sql = "SELECT Name, Type, NeighbourNumber FROM Drumuri";

    int rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
    int i;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    { 
        i=0;
        const char *nume = (const char *)sqlite3_column_text(stmt, 0);
        const char *tip = (const char *)sqlite3_column_text(stmt, 1);
        int nrneighbours = sqlite3_column_int(stmt, 5);
        while(i<3)
        {
            if((strcmp(tip, road_type[i])==0) && nrneighbours <= i+1)
            {
                Neighbour n;
                n.name = nume;
                n.km = sqlite3_column_int(stmt, 6);
                n.nrneighbours = sqlite3_column_int(stmt, 5);
                sqlite3_finalize(stmt);
                return n;
            }
            i++;#include <stdio.h>
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
char  weather[5][10] = {"sunny", "rainy", "foggy", "snowy", "windy"};
char  road_type[3][10] = {"autostrada", "drum", "oras"};
int clients[MAX_CLIENTS];
int client_count = 0;

typedef struct {
    char *name;
    int km,nrneighbours;
}Neighbour;

typedef struct{
    char *neighbours;
    char *intersectionpointneighbours;
}Formation_neighbour;

pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t db_lock = PTHREAD_MUTEX_INITIALIZER;

sqlite3 *db;

// Function to lock/unlock DB
void lock_db() {
    pthread_mutex_lock(&db_lock);
}

void unlock_db() {
    pthread_mutex_unlock(&db_lock);
}

void check_sqlite(int rc, const char *message);
void initialize_database();
char *get_nth_word(const char *input, int n);
char *check_bd_insert(const char *message, char *response);
char *insert_bd(const char *message, char *response);
int generatekm(char *message);
Neighbour get_avaible_neighbour_db();
char *check_bd_neighbour();
int min(int a, int b);
Formation_neighbour Formate(Formation_neighbour fn);

void process_client_message(const char *message, char *response)
{
    if (strstr(message, "locatie") != NULL)
    {
        check_bd_insert(message, response);
    }
    else if (strstr(message, "insert_drum") != NULL)
    {
        insert_bd(message, response);
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

char *get_nth_word(const char *input, int n)
{
    if (input == NULL || n <= 0)
    {
        return NULL;
    }

    char *input_copy = strdup(input);
    if (input_copy == NULL)
    {
        perror("Allocation failed");
        exit(EXIT_FAILURE);
    }

    char *token = strtok(input_copy, " ");
    int word_count = 1;

    while (token != NULL)
    {
        if (word_count == n)
        {
            char *nth_word = strdup(token);
            free(input_copy);
            return nth_word;
        }
        token = strtok(NULL, " ");
        word_count++;
    }

    free(input_copy);
    return NULL;
}

void initialize_database()
{
    // Deschidem baza de date
    int rc = sqlite3_open("trafic.db", &db);
    check_sqlite(rc, "opening database");

    // Cream tabelele
    const char *sql_create_tables =
        "CREATE TABLE IF NOT EXISTS Drumuri ("
        "  Name TEXT PRIMARY KEY, "
        "  Type TEXT, "
        "  Neighbours TEXT, "    // despartiti prin space
        "  IntersectionPointNeighbour TEXT, " // despartit tot prin space, ar trebui sa fie ordonat crescator, deci si neighbour ar trebui sa fie ordonat
        "  NeighbourNumber INTEGER, "
        "  TotalKms INTEGER, "
        "  GasStation INTEGER, "
        "  Crashes INTEGER, "
        "  Weather TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS Clienti ("
        "  clientid INTEGER PRIMARY KEY AUTOINCREMENT, "
        "  NameRoad TEXT, "
        "  LocationKm INTEGER,"
        "  Direction INTEGER "
        ");";

    rc = sqlite3_exec(db, sql_create_tables, 0, 0, NULL);
    check_sqlite(rc, "creating tables");

    printf("Database initialized successfully.\n");
}

char *check_bd_insert(const char *message, char *response)
{
    lock_db();  // Închidem lacătul pe BD
    sqlite3_stmt *stmt;
    const char *select_sql = "SELECT Name FROM Drumuri";

    int rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        snprintf(response, BUFFER_SIZE, "SQL error preparing statement: %s\n", sqlite3_errmsg(db));
        unlock_db();
        return response;
    }

    // Obținem cuvântul 2 din message (numele drumului pe care-l căutăm)
    char *road_name = get_nth_word(message, 2);
    if (!road_name)
    {
        snprintf(response, BUFFER_SIZE, "Nu ai trimis un nume de drum.\n");
        sqlite3_finalize(stmt);
        unlock_db();
        return response;
    }

    int check = 0;
    // Parcurgem rezultatele
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char *nume = (const char *)sqlite3_column_text(stmt, 0);
        // Comparam corect conținutul
        if (strcmp(nume, road_name) == 0)
        {
            check = 1;
            break;
        }
    }
    sqlite3_finalize(stmt);
    free(road_name); // eliberăm memorie, get_nth_word returnează strdup

    if (check == 1)
    {
        snprintf(response, BUFFER_SIZE, "V-am gasit locatia!\n");
    }
    else
    {
        snprintf(response, BUFFER_SIZE,
                 "Nu v-am gasit locatia in baza noastra de date. "
                 "Va rog inserati astfel: insert_drum <nume_drum> <tip_drum>.\n");
    }

    unlock_db();  // Eliberăm lacătul pe BD
    return response;
}

char *insert_bd(const char *message, char *response)
{
    lock_db();  // Închidem lacătul pe BD

    // Obținem parametrii 2 și 3 din mesaj
    char *road_name = get_nth_word(message, 2);
    char *road_type = get_nth_word(message, 3);

    if (!road_name || !road_type)
    {
        snprintf(response, BUFFER_SIZE, "Parametri insuficienti. Folositi: insert_drum <nume> <tip>.\n");
        if (road_name) free(road_name);
        if (road_type) free(road_type);
        unlock_db();
        return response;
    }

    const char *insert_sql = 
        "INSERT INTO Drumuri (Name, Type, Neighbours, IntersectionPointNeighbour, NeighbourNumber, TotalKms, GasStation, Crashes, Weather) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    const char * update_sql = 
        "UPDATE Drumuri SET Neighbours = ?, IntersectionPointNeighbour = ?, NeighbourNumber = ? WHERE Name = ?";
        "VALUES (?, ?, ?, ?);";

    sqlite3_stmt *stmt;
    sqlite3_stmt *stmt2;
    int nc = sqlite3_prepare_v2(db, update_sql, -1, &stmt2, NULL);
    int rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK && nc != SQLITE_OK)
    {
        snprintf(response, BUFFER_SIZE, "SQL error preparing statement: %s\n", sqlite3_errmsg(db));
        free(road_name);
        free(road_type);
        unlock_db();
        return response;
    }
    int km = generatekm(road_type), neighborkm;
    Neighbour n = get_avaible_neighbour_db();
    // Legăm parametrii folosind SQLITE_TRANSIENT, astfel SQLite își face singur copie
    sqlite3_bind_text(stmt, 1, road_name, -1, SQLITE_TRANSIENT); // nume
    sqlite3_bind_text(stmt, 2, road_type, -1, SQLITE_TRANSIENT); // tip
    sqlite3_bind_text(stmt, 3, n.name, -1, SQLITE_TRANSIENT); // vecini
    sqlite3_bind_int(stmt, 4, rand() %min(n.km,km), -1, SQLITE_TRANSIENT); // nodul intersectie cu vecini
    // am adaugat vecini pentru drumul pe care il inserez, mai trb sa adaug si pentru drumul care se va invecina acum cu el
    sqlite3_bind_int(stmt, 5, 1); // numar vecini totali
    sqlite3_bind_text(stmt, 6, km); // total km drum
    sqlite3_bind_int(stmt, 7, rand() % km); // benzinarie
    sqlite3_bind_text(stmt, 8, 0); // accidente
    sqlite3_bind_text(stmt, 9, weather[rand() % 5], -1, SQLITE_TRANSIENT); //vreme
    rc = sqlite3_step(stmt);
    sqlite3_bind_text(stmt2, 4, n.name, -1, SQLITE_TRANSIENT); // nume
    sqlite3_bind_text(stmt2, 3, n.nrneighbours+1); // numar vecini
    Formation_neighbour fn;
    Formate(fn);
    sqlite3_bind_text(stmt2, 1, fn.neighbours, -1, SQLITE_TRANSIENT); // vecini
    sqlite3_bind_text(stmt2, 2, fn.intersectionpointneighbours, -1, SQLITE_TRANSIENT); // nodul intersectie cu vecini
    nc = sqlite3_step(stmt2);
    if (rc == SQLITE_DONE && nc == SQLITE_DONE)
    {
        snprintf(response, BUFFER_SIZE, "Drum adăugat cu succes.\n");
    }
    else
    {
        snprintf(response, BUFFER_SIZE, "Eroare la adăugare: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    free(road_name);
    free(road_type);

    unlock_db(); 
    return response;
}


void check_sqlite(int rc, const char *message)
{
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Error %s: %s\n", message, sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
}

int generatekm(char *message)
{
    int km = 0;
    if (strcmp(message, "autostrada") == 0)
    {
        km = rand() % 75 + 200;
    }
    else if (strcmp(message, "drum") == 0)
    {
        km = rand() % 75 + 100;
    }
    else if (strcmp(message, "oras") == 0)
    {
        km = rand() % 50 + 75;
    }
    return km;
}


Neighbour get_avaible_neighbour_db()
{
    sqlite3_stmt *stmt;
    const char *select_sql = "SELECT Name, Type, NeighbourNumber FROM Drumuri";

    int rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
    int i;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    { 
        i=0;
        const char *nume = (const char *)sqlite3_column_text(stmt, 0);
        const char *tip = (const char *)sqlite3_column_text(stmt, 1);
        int nrneighbours = sqlite3_column_int(stmt, 5);
        while(i<3)
        {
            if((strcmp(tip, road_type[i])==0) && nrneighbours <= i+1)
            {
                Neighbour n;
                n.name = nume;
                n.km = sqlite3_column_int(stmt, 6);
                n.nrneighbours = sqlite3_column_int(stmt, 5);
                sqlite3_finalize(stmt);
                return n;
            }
            i++;
        }
    }
}


int min(int a, int b)
{
    if(a<b)
    {
        return a;
    }
    return b;
}
        }
    }
}


int min(int a, int b)
{
    if(a<b)
    {
        return a;
    }
    return b;
}