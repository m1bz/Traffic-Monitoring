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
    int  km;     
    int  nrneighbours;
} Neighbour;

typedef struct {
    char *neighbours;
    char *intersectionpointneighbours;
} Formation_neighbour;

pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t db_lock = PTHREAD_MUTEX_INITIALIZER;
sqlite3 *db;

void lock_db() {
    pthread_mutex_lock(&db_lock);
}

void unlock_db() {
    pthread_mutex_unlock(&db_lock);
}
void check_sqlite(int rc, const char *message);
char *get_nth_word(const char *input, int n);
int generatekm(const char *type);
int min(int a, int b);
Formation_neighbour Formate(const char *old_neighbours, const char *old_intersections, const char *new_name, int new_intersection);
Neighbour get_avaible_neighbour_db();
char *insert_bd(const char *message, char *response);
void initialize_database();
char *check_bd_insert(const char *message, char *response);
void process_client_message(const char *message, char *response);
void *handle_client(void *arg);


int main()
{
    srand((unsigned int)time(NULL));  // Inițializează random

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





void check_sqlite(int rc, const char *message)
{
    if (rc != SQLITE_OK && rc != SQLITE_DONE)
    {
        fprintf(stderr, "Error %s: %s\n", message, sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
}


char *get_nth_word(const char *input, int n)
{
    if (input == NULL || n <= 0) {
        return NULL;
    }
    char *input_copy = strdup(input);
    if (input_copy == NULL) {
        perror("Allocation failed");
        return NULL;
    }

    char *token = strtok(input_copy, " ");
    int word_count = 1;
    char *nth_word = NULL;

    while (token != NULL) {
        if (word_count == n) {
            nth_word = strdup(token);
            break;
        }
        token = strtok(NULL, " ");
        word_count++;
    }

    free(input_copy);
    return nth_word;
}


int generatekm(const char *type)
{
    int km = 0;
    if (strcmp(type, "autostrada") == 0)
    {
        km = rand() % 75 + 200;
    }
    else if (strcmp(type, "drum") == 0)
    {
        km = rand() % 75 + 100;
    }
    else if (strcmp(type, "oras") == 0)
    {
        km = rand() % 50 + 75;
    }
    return km;
}


int min(int a, int b)
{
    return (a < b) ? a : b;
}


Formation_neighbour Formate(const char *old_neighbours,const char *old_intersections,const char *new_name,int new_intersection)
{
    Formation_neighbour fn;
    fn.neighbours = NULL;
    fn.intersectionpointneighbours = NULL;

    // 1) Spargem vechile neighbours
    // --------------------------------------------------------
    // să aflăm cîte drumuri aveam
    int count = 0;
    char *temp_neigh = strdup(old_neighbours ? old_neighbours : "");
    char *temp_inters = strdup(old_intersections ? old_intersections : "");
    // Handle cazul cînd e empty string
    if(!temp_neigh) temp_neigh = strdup("");
    if(!temp_inters) temp_inters = strdup("");

    // vom folosi un vector de char* (pentru nume) și un vector de int (pentru intersections)
    char **name_array = (char **)calloc(100, sizeof(char*)); // exemplu

    // parse neighbours
    char *saveptr1;
    char *token = strtok_r(temp_neigh, " ", &saveptr1);
    while(token != NULL) {
        name_array[count] = strdup(token);
        count++;
        token = strtok_r(NULL, " ", &saveptr1);
    }

    // parse intersection points
    int *int_ptrs = (int*)calloc(count, sizeof(int));
    int i = 0;
    char *saveptr2;
    token = strtok_r(temp_inters, " ", &saveptr2);
    while(token != NULL && i < count) {
        int_ptrs[i] = atoi(token);
        i++;
        token = strtok_r(NULL, " ", &saveptr2);
    }

    // 2) Adăugăm noul element (drum + intersection)
    // --------------------------------------------------------
    name_array[count] = strdup(new_name);
    int_ptrs[count]   = new_intersection;
    count++;

    // 3) Sortăm crescător după intersectionpoint
    // --------------------------------------------------------
    // bubble sort / insertion sort / etc., exemplu simplu bubble:
    for(int pass = 0; pass < count - 1; pass++) {
        for(int j = 0; j < count - pass - 1; j++) {
            if(int_ptrs[j] > int_ptrs[j+1]) {
                // swap intersection
                int tmp_i = int_ptrs[j];
                int_ptrs[j] = int_ptrs[j+1];
                int_ptrs[j+1] = tmp_i;
                // swap name_array
                char *tmp_s = name_array[j];
                name_array[j] = name_array[j+1];
                name_array[j+1] = tmp_s;
            }
        }
    }

    // 4) Reconstruim string-urile finale
    // --------------------------------------------------------
    // neighbours
    size_t final_size = 0;
    for(int k = 0; k < count; k++) {
        final_size += strlen(name_array[k]) + 2; // spatiu + \0
    }
    char *final_neigh = (char *)calloc(final_size, sizeof(char));
    final_neigh[0] = '\0';

    // intersectionpoints
    size_t final_size2 = count * 12; // ~ spațiu pentru int-uri + spații
    char *final_inters_str = (char *)calloc(final_size2, sizeof(char));
    final_inters_str[0] = '\0';

    // concatenăm
    for(int k = 0; k < count; k++) {
        strcat(final_neigh, name_array[k]);
        if(k < count - 1) strcat(final_neigh, " ");

        char buffer_int[32];
        sprintf(buffer_int, "%d", int_ptrs[k]);
        strcat(final_inters_str, buffer_int);
        if(k < count - 1) strcat(final_inters_str, " ");
    }

    fn.neighbours = strdup(final_neigh);
    fn.intersectionpointneighbours = strdup(final_inters_str);

    // 5) Curățăm memoria
    // --------------------------------------------------------
    for(int k = 0; k < count; k++) {
        free(name_array[k]);
    }
    free(name_array);
    free(int_ptrs);
    free(final_neigh);
    free(final_inters_str);
    free(temp_neigh);
    free(temp_inters);

    return fn;
}

Neighbour get_avaible_neighbour_db()
{
    Neighbour result;
    result.name = NULL;
    result.km   = 0;
    result.nrneighbours = 0;

    sqlite3_stmt *stmt;
    const char *select_sql = "SELECT Name, Type, NeighbourNumber, TotalKms FROM Drumuri;";
    int rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
    check_sqlite(rc, "prepare select in get_avaible_neighbour_db");

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char *nume  = (const char *)sqlite3_column_text(stmt, 0);
        const char *tip   = (const char *)sqlite3_column_text(stmt, 1);
        int nrnb          = sqlite3_column_int(stmt, 2);
        int totalkms      = sqlite3_column_int(stmt, 3);
        if (nrnb <= 2) {
            result.name = strdup(nume); 
            result.km   = totalkms;
            result.nrneighbours = nrnb;
            break;
        }
    }
    sqlite3_finalize(stmt);

    return result;
}


char *insert_bd(const char *message, char *response)
{
    lock_db();  // Închidem lacătul pe BD

    char *road_name = get_nth_word(message, 2); 
    char *road_type_str = get_nth_word(message, 3);

    if (!road_name || !road_type_str) {
        snprintf(response, BUFFER_SIZE,
                 "Parametri insuficienti. Folositi: insert_drum <nume> <tip>.\n");
        if (road_name)       free(road_name);
        if (road_type_str)   free(road_type_str);
        unlock_db();
        return response;
    }

    // 1) Verificăm dacă există deja drumuri
    int count_drumuri = 0;
    {
        sqlite3_stmt *stmt_check;
        const char *sql_count = "SELECT COUNT(*) FROM Drumuri;";
        int rc = sqlite3_prepare_v2(db, sql_count, -1, &stmt_check, NULL);
        if (rc == SQLITE_OK) {
            if (sqlite3_step(stmt_check) == SQLITE_ROW) {
                count_drumuri = sqlite3_column_int(stmt_check, 0);
            }
        }
        sqlite3_finalize(stmt_check);
    }

    sqlite3_stmt *stmt_check;
    const char *check_sql = "SELECT COUNT(*) FROM Drumuri WHERE Name = ?;";
    int rc = sqlite3_prepare_v2(db, check_sql, -1, &stmt_check, NULL);
    if (rc != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "SQL error preparing check statement: %s\n", sqlite3_errmsg(db));
        free(road_name);
        free(road_type_str);
        unlock_db();
        return response;
    }

    sqlite3_bind_text(stmt_check, 1, road_name, -1, SQLITE_TRANSIENT);
    int exists = 0;

    if (sqlite3_step(stmt_check) == SQLITE_ROW) {
        exists = sqlite3_column_int(stmt_check, 0);
    }
    sqlite3_finalize(stmt_check);

    if (exists > 0) {
        snprintf(response, BUFFER_SIZE,
                 "Drumul '%s' exista deja in baza de date. Nu a fost adaugat din nou.\n",
                 road_name);
        free(road_name);
        free(road_type_str);
        unlock_db();
        return response;
    }

    // 2) Pregătim "INSERT" pentru noul drum
    const char *insert_sql = 
        "INSERT INTO Drumuri (Name, Type, Neighbours, IntersectionPointNeighbour, "
        "                     NeighbourNumber, TotalKms, GasStation, Crashes, Weather) "
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt_insert;
    int rc_insert = sqlite3_prepare_v2(db, insert_sql, -1, &stmt_insert, NULL);
    if (rc_insert != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "SQL error preparing insert: %s\n",
                 sqlite3_errmsg(db));
        free(road_name);
        free(road_type_str);
        unlock_db();
        return response;
    }

    // 3) Dacă nu există niciun drum în DB, nu setăm niciun vecin
    if (count_drumuri == 0)
    {
        int km = generatekm(road_type_str);
        // Bind parametrii
        sqlite3_bind_text(stmt_insert, 1, road_name, -1, SQLITE_TRANSIENT);  // Name
        sqlite3_bind_text(stmt_insert, 2, road_type_str, -1, SQLITE_TRANSIENT); // Type
        // Neighbours -> "", IntersectionPointNeighbour -> ""
        sqlite3_bind_text(stmt_insert, 3, "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_insert, 4, "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt_insert, 5, 0);              // NeighbourNumber
        sqlite3_bind_int(stmt_insert, 6, km);             // TotalKms
        sqlite3_bind_int(stmt_insert, 7, rand() % km);    // GasStation (random location pe drum)
        sqlite3_bind_int(stmt_insert, 8, 0);              // Crashes
        sqlite3_bind_text(stmt_insert, 9, weather[rand() % 5], -1, SQLITE_TRANSIENT);

        rc_insert = sqlite3_step(stmt_insert);
        check_sqlite(rc_insert, "insert new road first time");
        sqlite3_finalize(stmt_insert);

        snprintf(response, BUFFER_SIZE,
                 "Primul drum adăugat cu succes (%s), fără vecini.\n", road_name);

        free(road_name);
        free(road_type_str);
        unlock_db();
        return response;
    }
    else
    {
        // 4) Extrag un drum eligibil ca vecin
        Neighbour n = get_avaible_neighbour_db();
        if (!n.name) {
            // Dacă n-am găsit niciun vecin eligibil, inserez oricum fără vecini
            int km = generatekm(road_type_str);
            sqlite3_bind_text(stmt_insert, 1, road_name, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt_insert, 2, road_type_str, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt_insert, 3, "", -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt_insert, 4, "", -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt_insert, 5, 0);
            sqlite3_bind_int(stmt_insert, 6, km);
            sqlite3_bind_int(stmt_insert, 7, rand() % km);
            sqlite3_bind_int(stmt_insert, 8, 0);
            sqlite3_bind_text(stmt_insert, 9, weather[rand() % 5], -1, SQLITE_TRANSIENT);

            rc_insert = sqlite3_step(stmt_insert);
            check_sqlite(rc_insert, "insert new road with no eligible neighbour");
            sqlite3_finalize(stmt_insert);

            snprintf(response, BUFFER_SIZE,
                     "Drum adăugat fără vecin (nu s-a găsit nimic eligibil).\n");
            free(road_name);
            free(road_type_str);
            unlock_db();
            return response;
        }

        // 5) Inserez noul drum cu un (1) vecin: n.name
        //    Trebuie să aleg intersection point random
        int kmNew = generatekm(road_type_str);
        int intersection = rand() % min(n.km, kmNew);

        // Pentru a insersa corect, trebuie să știu și neighbours + intersectionpoint
        // = "n.name" și intersection
        // cum e doar 1, totul e direct
        char intersection_str[32];
        sprintf(intersection_str, "%d", intersection);

        // Bind pt noul drum
        sqlite3_bind_text(stmt_insert, 1, road_name, -1, SQLITE_TRANSIENT);   // Name
        sqlite3_bind_text(stmt_insert, 2, road_type_str, -1, SQLITE_TRANSIENT);// Type
        sqlite3_bind_text(stmt_insert, 3, n.name, -1, SQLITE_TRANSIENT);      // Neighbours (un singur vecin)
        sqlite3_bind_text(stmt_insert, 4, intersection_str, -1, SQLITE_TRANSIENT); // IntersectionPointNeighbour
        sqlite3_bind_int(stmt_insert, 5, 1);                                  // NeighbourNumber
        sqlite3_bind_int(stmt_insert, 6, kmNew);                              // TotalKms
        sqlite3_bind_int(stmt_insert, 7, rand() % kmNew);                     // GasStation
        sqlite3_bind_int(stmt_insert, 8, 0);                                  // Crashes
        sqlite3_bind_text(stmt_insert, 9, weather[rand() % 5], -1, SQLITE_TRANSIENT); // Weather

        rc_insert = sqlite3_step(stmt_insert);
        check_sqlite(rc_insert, "insert new road");
        sqlite3_finalize(stmt_insert);

        // 6) Actualizez vecinul. Trebuie să citesc vechii neighbours și intersection points ai lui
        char old_neighbours[1024] = "";
        char old_inters[1024]     = "";
        int old_nb_number = 0;

        {
            // Îi citesc datele direct din DB
            sqlite3_stmt *stmt_old;
            const char *sel_sql =
              "SELECT Neighbours, IntersectionPointNeighbour, NeighbourNumber "
              "FROM Drumuri WHERE Name=?;";
            rc_insert = sqlite3_prepare_v2(db, sel_sql, -1, &stmt_old, NULL);
            if(rc_insert == SQLITE_OK)
            {
                sqlite3_bind_text(stmt_old, 1, n.name, -1, SQLITE_TRANSIENT);
                if(sqlite3_step(stmt_old) == SQLITE_ROW)
                {
                    const char *nb = (const char *)sqlite3_column_text(stmt_old, 0);
                    const char *in = (const char *)sqlite3_column_text(stmt_old, 1);
                    old_nb_number  = sqlite3_column_int(stmt_old, 2);

                    if(nb) strncpy(old_neighbours, nb, sizeof(old_neighbours)-1);
                    if(in) strncpy(old_inters,    in, sizeof(old_inters)-1);
                }
            }
            sqlite3_finalize(stmt_old);
        }

        // Construiesc noile stringuri (neighbours + intersection) cu function "Formate"
        Formation_neighbour fn = Formate(old_neighbours,
                                         old_inters,
                                         road_name,     // adaug la vecin și drumul nou creat
                                         intersection); // intersection point comun
        // no. neighbours
        int new_nb_number = old_nb_number + 1;

        // 7) Fac UPDATE la drumului vecin
        const char *update_sql =
          "UPDATE Drumuri SET Neighbours=?, IntersectionPointNeighbour=?, NeighbourNumber=? "
          " WHERE Name=?;";
        sqlite3_stmt *stmt_up;
        rc_insert = sqlite3_prepare_v2(db, update_sql, -1, &stmt_up, NULL);
        check_sqlite(rc_insert, "prepare update neighbour");

        sqlite3_bind_text(stmt_up, 1, fn.neighbours, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_up, 2, fn.intersectionpointneighbours, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt_up, 3, new_nb_number);
        sqlite3_bind_text(stmt_up, 4, n.name, -1, SQLITE_TRANSIENT);

        rc_insert = sqlite3_step(stmt_up);
        check_sqlite(rc_insert, "update neighbour step");
        sqlite3_finalize(stmt_up);

        snprintf(response, BUFFER_SIZE, "Drum '%s' adăugat cu succes. Vecin: '%s'.\n",
                 road_name, n.name);

        // Curăț memorie
        free(n.name);   // alocat cu strdup în get_avaible_neighbour_db()
        free(road_name);
        free(road_type_str);
        free(fn.neighbours);
        free(fn.intersectionpointneighbours);

        unlock_db();
        return response;
    }
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
        "  Neighbours TEXT, "    // despărțiți prin space
        "  IntersectionPointNeighbour TEXT, " 
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
    lock_db();
    sqlite3_stmt *stmt;
    const char *select_sql = "SELECT Name FROM Drumuri";
    int rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "SQL error preparing statement: %s\n", sqlite3_errmsg(db));
        unlock_db();
        return response;
    }

    // Obținem al doilea cuvînt (numele drumului)
    char *road_name = get_nth_word(message, 2);
    if (!road_name) {
        snprintf(response, BUFFER_SIZE, "Nu ai trimis un nume de drum.\n");
        sqlite3_finalize(stmt);
        unlock_db();
        return response;
    }

    int check = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char *nume = (const char *)sqlite3_column_text(stmt, 0);
        if (strcmp(nume, road_name) == 0) {
            check = 1;
            break;
        }
    }
    sqlite3_finalize(stmt);
    free(road_name);

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
    unlock_db();
    return response;
}

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
