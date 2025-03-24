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
#include <time.h>

#define PORT 2728
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 100

char weather[5][10] = {"sunny", "rainy", "foggy", "snowy", "windy"};
char sportsInfo[5][50] = {
    "Echipa Romaniei a castigat un mare turneu.",
    "Jucatorul Stanciu a marcat un hattrick!",
    "Record mondial stabilit in proba de 100m!",
    "Campionatul de tenis s-a incheiat aseara.",
    "Baschet: Echipa Antur a invins in finala."
};

int clients[MAX_CLIENTS];
bool is_located[MAX_CLIENTS];

pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t db_lock = PTHREAD_MUTEX_INITIALIZER;
sqlite3 *db;

typedef struct {
    char *name;
    int km;
    int nrneighbours;
} Neighbour;

typedef struct {
    char *neighbours;
    char *intersectionpointneighbours;
} Formation_neighbour;

void lock_db();
void unlock_db();
void check_sqlite(int rc, const char *message);

char *get_nth_word(const char *input, int n);

float generategasprices();
int generatekm(const char *type);
int generatespeed(const char *type);

int min_int(int a, int b);

Formation_neighbour Formate(const char *old_neighbours, const char *old_intersections,const char *new_name,int new_intersection);

Neighbour get_avaible_neighbour_db();
char *insert_bd(const char *message, char *response);

void initialize_database();
char *check_bd_insert(const char *message, char *response);

void register_client_in_db(int client_socket);
void unregister_client_in_db(int client_socket);
void insert_client_location(int client_socket, const char *road_name, int km, int speed, char *response);

void broadcast_message(const char *msg);
void accident_report(int client_socket, char *response);
void clear_all_crashes(char *response);
void show_all_drums(char *response);

void cmd_gas(int client_socket, char *response);
void cmd_weather(int client_socket, char *response);
void cmd_sports(int client_socket, char *response);
void cmd_allinfo(int client_socket, char *response);

void check_and_recommend_speed(int client_socket, char *response);
void cmd_viteza(int client_socket, int newSpeed, char *response);

void update_all_clients_speed_for_road(const char *roadName);
void update_all_clients_speed_for_all_roads();

void process_client_message(const char *message, char *response, int client_socket);
void *handle_client(void *arg);

int main()
{
    srand((unsigned int)time(NULL));

    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    initialize_database();

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i] = -1;
        is_located[i] = false;
    }

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, MAX_CLIENTS) == -1) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    printf("Server listening on port %d\n", PORT);

    while (1) {
        int *new_socket = malloc(sizeof(int));
        *new_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (*new_socket < 0) {
            perror("Accept failed");
            free(new_socket);
            continue;
        }
        printf("New connection: IP %s, Port %d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        pthread_mutex_lock(&clients_lock);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] == -1) {
                clients[i] = *new_socket;
                is_located[i] = false;
                break;
            }
        }
        pthread_mutex_unlock(&clients_lock);

        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, new_socket);
        pthread_detach(thread_id);
    }

    sqlite3_close(db);
    close(server_socket);
    return 0;
}

void lock_db() {
    pthread_mutex_lock(&db_lock);
}
void unlock_db() {
    pthread_mutex_unlock(&db_lock);
}

void check_sqlite(int rc, const char *message)
{
    if (rc != SQLITE_OK && rc != SQLITE_DONE) {
        fprintf(stderr, "Error %s: %s\n", message, sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
}

char *get_nth_word(const char *input, int n)
{
    if (!input || n <= 0)
        return NULL;

    char *input_copy = strdup(input);
    if (!input_copy) {
        perror("Allocation failed");
        return NULL;
    }
    char *token = strtok(input_copy, " ");
    int word_count = 1;
    char *nth_word = NULL;

    while (token) {
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

float generategasprices() {
    float minVal = 5.0f, maxVal = 8.0f;
    float random_value = minVal + (((float)rand()/(float)RAND_MAX))*(maxVal - minVal);
    int temp = (int)(random_value * 100.0f);
    float rounded = temp / 100.0f;
    return rounded;
}

int generatekm(const char *type)
{
    int km = 0;
    if (strcmp(type, "autostrada") == 0)
        km = rand() % 75 + 200;
    else if (strcmp(type, "drum") == 0)
        km = rand() % 75 + 100;
    else if (strcmp(type, "oras") == 0)
        km = rand() % 50 + 75;
    else
        km = rand() % 75 + 100;
    if (km <= 0)
        km = 100;
    return km;
}

int generatespeed(const char *type)
{
    if (strcmp(type, "autostrada") == 0)
        return 130;
    else if (strcmp(type, "drum") == 0)
        return 90;
    else if (strcmp(type, "oras") == 0)
        return 50;
    return 50;
}

int min_int(int a, int b)
{
    return (a < b) ? a : b;
}

Formation_neighbour Formate(const char *old_neighbours,
                            const char *old_intersections,
                            const char *new_name,
                            int new_intersection)
{
    Formation_neighbour fn;
    fn.neighbours = NULL;
    fn.intersectionpointneighbours = NULL;

    int count = 0;
    char *temp_neigh = strdup(old_neighbours ? old_neighbours : "");
    char *temp_inters = strdup(old_intersections ? old_intersections : "");
    if(!temp_neigh)  temp_neigh  = strdup("");
    if(!temp_inters) temp_inters = strdup("");

    char **name_array = (char **)calloc(100, sizeof(char*));
    char *saveptr1;
    char *token = strtok_r(temp_neigh, " ", &saveptr1);
    while (token) {
        name_array[count] = strdup(token);
        count++;
        token = strtok_r(NULL, " ", &saveptr1);
    }
    int *int_ptrs = (int*)calloc(count, sizeof(int));
    int i = 0;
    char *saveptr2;
    token = strtok_r(temp_inters, " ", &saveptr2);
    while (token && i < count) {
        int_ptrs[i] = atoi(token);
        i++;
        token = strtok_r(NULL, " ", &saveptr2);
    }

    name_array[count] = strdup(new_name);
    int_ptrs[count] = new_intersection;
    count++;

    for (int pass = 0; pass < count - 1; pass++) {
        for (int j = 0; j < count - pass - 1; j++) {
            if (int_ptrs[j] > int_ptrs[j+1]) {
                int tmp_i = int_ptrs[j];
                int_ptrs[j] = int_ptrs[j+1];
                int_ptrs[j+1] = tmp_i;
                char *tmp_s = name_array[j];
                name_array[j] = name_array[j+1];
                name_array[j+1] = tmp_s;
            }
        }
    }

    size_t final_size = 0;
    for (int k = 0; k < count; k++)
        final_size += strlen(name_array[k]) + 2;
    char *final_neigh = (char *)calloc(final_size, sizeof(char));
    final_neigh[0] = '\0';

    size_t final_size2 = count * 12;
    char *final_inters_str = (char *)calloc(final_size2, sizeof(char));
    final_inters_str[0] = '\0';

    for (int k = 0; k < count; k++) {
        strcat(final_neigh, name_array[k]);
        if (k < count - 1)
            strcat(final_neigh, " ");

        char buffer_int[32];
        sprintf(buffer_int, "%d", int_ptrs[k]);
        strcat(final_inters_str, buffer_int);
        if (k < count - 1)
            strcat(final_inters_str, " ");
    }

    fn.neighbours = strdup(final_neigh);
    fn.intersectionpointneighbours = strdup(final_inters_str);

    for (int k = 0; k < count; k++)
        free(name_array[k]);
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
    result.km = 0;
    result.nrneighbours = 0;

    sqlite3_stmt *stmt;
    const char *select_sql = "SELECT Name, Type, NeighbourNumber, TotalKms FROM Drumuri;";
    int rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
    check_sqlite(rc, "prepare select in get_avaible_neighbour_db");

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *nume = (const char *)sqlite3_column_text(stmt, 0);
        int nrnb = sqlite3_column_int(stmt, 2);
        int totalkms = sqlite3_column_int(stmt, 3);

        if (nrnb <= 2) {
            result.name = strdup(nume);
            result.km = totalkms;
            result.nrneighbours = nrnb;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

char *insert_bd(const char *message, char *response)
{
    lock_db();

    char *road_name = get_nth_word(message, 2);
    char *road_type_str = get_nth_word(message, 3);

    if (!road_name || !road_type_str) {
        snprintf(response, BUFFER_SIZE,
                 "Parametri insuficienti. Folositi: insert_drum <nume> <tip>.\n");
        if (road_name) free(road_name);
        if (road_type_str) free(road_type_str);
        unlock_db();
        return response;
    }

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

    const char *insert_sql =
        "INSERT INTO Drumuri (Name, Type, Neighbours, IntersectionPointNeighbour, "
        "                     NeighbourNumber, TotalKms, GasStation, GasPrices, Crashes, Weather, Speed) "
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt_insert;
    int rc_insert = sqlite3_prepare_v2(db, insert_sql, -1, &stmt_insert, NULL);
    if (rc_insert != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "SQL error preparing insert: %s\n", sqlite3_errmsg(db));
        free(road_name);
        free(road_type_str);
        unlock_db();
        return response;
    }

    if (count_drumuri == 0)
    {
        int km = generatekm(road_type_str);
        sqlite3_bind_text(stmt_insert, 1, road_name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_insert, 2, road_type_str, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_insert, 3, "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_insert, 4, "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt_insert, 5, 0);
        sqlite3_bind_int(stmt_insert, 6, km);

        float gas_station_km = ((float)rand()/RAND_MAX) * km;
        sqlite3_bind_double(stmt_insert, 7, gas_station_km);

        float gas_p = generategasprices();
        sqlite3_bind_double(stmt_insert, 8, gas_p);

        sqlite3_bind_int(stmt_insert, 9, 0);
        sqlite3_bind_text(stmt_insert, 10, weather[rand() % 5], -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt_insert, 11, generatespeed(road_type_str));

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
        Neighbour n = get_avaible_neighbour_db();
        if (!n.name) {
            int km = generatekm(road_type_str);
            sqlite3_bind_text(stmt_insert, 1, road_name, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt_insert, 2, road_type_str, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt_insert, 3, "", -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt_insert, 4, "", -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt_insert, 5, 0);
            sqlite3_bind_int(stmt_insert, 6, km);

            float gas_station_km = ((float)rand()/RAND_MAX) * km;
            sqlite3_bind_double(stmt_insert, 7, gas_station_km);

            float gas_p = generategasprices();
            sqlite3_bind_double(stmt_insert, 8, gas_p);

            sqlite3_bind_int(stmt_insert, 9, 0);
            sqlite3_bind_text(stmt_insert, 10, weather[rand() % 5], -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt_insert, 11, generatespeed(road_type_str));

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

        int kmNew = generatekm(road_type_str);
        int intersection = rand() % min_int(n.km, kmNew);

        char intersection_str[32];
        sprintf(intersection_str, "%d", intersection);

        sqlite3_bind_text(stmt_insert, 1, road_name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_insert, 2, road_type_str, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_insert, 3, n.name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_insert, 4, intersection_str, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt_insert, 5, 1);
        sqlite3_bind_int(stmt_insert, 6, kmNew);

        float gas_station_km = ((float)rand()/RAND_MAX) * kmNew;
        sqlite3_bind_double(stmt_insert, 7, gas_station_km);

        float gas_p = generategasprices();
        sqlite3_bind_double(stmt_insert, 8, gas_p);

        sqlite3_bind_int(stmt_insert, 9, 0);
        sqlite3_bind_text(stmt_insert, 10, weather[rand() % 5], -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt_insert, 11, generatespeed(road_type_str));

        rc_insert = sqlite3_step(stmt_insert);
        check_sqlite(rc_insert, "insert new road");
        sqlite3_finalize(stmt_insert);

        char old_neighbours[1024] = "";
        char old_inters[1024] = "";
        int old_nb_number = 0;

        {
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
                    old_nb_number = sqlite3_column_int(stmt_old, 2);

                    if(nb) strncpy(old_neighbours, nb, sizeof(old_neighbours)-1);
                    if(in) strncpy(old_inters, in, sizeof(old_inters)-1);
                }
            }
            sqlite3_finalize(stmt_old);
        }

        Formation_neighbour fn = Formate(old_neighbours, old_inters, road_name, intersection);
        int new_nb_number = old_nb_number + 1;

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

        free(n.name);
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
    int rc = sqlite3_open("trafic.db", &db);
    check_sqlite(rc, "opening database");

    const char *sql_create_tables =
        "CREATE TABLE IF NOT EXISTS Drumuri ("
        "  Name TEXT PRIMARY KEY, "
        "  Type TEXT, "
        "  Neighbours TEXT, "
        "  IntersectionPointNeighbour TEXT, "
        "  NeighbourNumber INTEGER, "
        "  TotalKms INTEGER, "
        "  GasStation REAL, "
        "  GasPrices REAL, "
        "  Crashes INTEGER NULL, "
        "  Weather TEXT, "
        "  Speed INTEGER"
        ");"
        "CREATE TABLE IF NOT EXISTS Clienti ("
        "  clientid INTEGER PRIMARY KEY, "
        "  NameRoad TEXT, "
        "  LocationKm INTEGER, "
        "  Speed INTEGER, "
        "  GasInfo INTEGER, "
        "  WeatherInfo INTEGER, "
        "  SportsInfo INTEGER"
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
    char *road_name = get_nth_word(message, 2);
    if (!road_name) {
        snprintf(response, BUFFER_SIZE, "Nu ai trimis un nume de drum.\n");
        sqlite3_finalize(stmt);
        unlock_db();
        return response;
    }
    int check = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *nume = (const char *)sqlite3_column_text(stmt, 0);
        if (strcmp(nume, road_name) == 0) {
            check = 1;
            break;
        }
    }
    sqlite3_finalize(stmt);
    if (check == 1) {
        snprintf(response, BUFFER_SIZE, "V-am gasit locatia!\n");
    }
    else {
        snprintf(response, BUFFER_SIZE,
                 "Nu v-am gasit locatia in baza noastra de date. "
                 "Va rog inserati astfel: insert_drum <nume_drum> <tip_drum>.\n");
    }
    free(road_name);
    unlock_db();
    return response;
}

void register_client_in_db(int client_socket)
{
    lock_db();
    const char *sql_del = "DELETE FROM Clienti WHERE clientid=?;";
    sqlite3_stmt *stmt_del;
    int rc = sqlite3_prepare_v2(db, sql_del, -1, &stmt_del, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt_del, 1, client_socket);
        sqlite3_step(stmt_del);
        sqlite3_finalize(stmt_del);
    }
    const char *sql_ins =
        "INSERT INTO Clienti (clientid, NameRoad, LocationKm, Speed, GasInfo, WeatherInfo, SportsInfo) "
        "VALUES (?, '', 0, 0, 0, 0, 0);";
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, sql_ins, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error preparing register statement: %s\n", sqlite3_errmsg(db));
        unlock_db();
        return;
    }
    sqlite3_bind_int(stmt, 1, client_socket);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error inserting client record: %s\n", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    unlock_db();
}

void unregister_client_in_db(int client_socket)
{
    lock_db();
    const char *sql = "DELETE FROM Clienti WHERE clientid=?;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error preparing unregister statement: %s\n", sqlite3_errmsg(db));
        unlock_db();
        return;
    }
    sqlite3_bind_int(stmt, 1, client_socket);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error deleting client record: %s\n", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    unlock_db();
}

void insert_client_location(int client_socket, const char *road_name, int km, int speed, char *response)
{
    lock_db();
    sqlite3_stmt *stmt_sel;
    const char *sel_sql = "SELECT Weather, Crashes FROM Drumuri WHERE Name=?;";
    int rc = sqlite3_prepare_v2(db, sel_sql, -1, &stmt_sel, NULL);
    if (rc != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "Eroare pregatire select pentru drum.\n");
        unlock_db();
        return;
    }
    sqlite3_bind_text(stmt_sel, 1, road_name, -1, SQLITE_TRANSIENT);
    char road_weather[32] = "";
    int crash_val = -1;
    if (sqlite3_step(stmt_sel) == SQLITE_ROW) {
        const char *w = (const char *)sqlite3_column_text(stmt_sel, 0);
        if (w)
            strncpy(road_weather, w, sizeof(road_weather)-1);
        if (sqlite3_column_type(stmt_sel, 1) != SQLITE_NULL)
            crash_val = sqlite3_column_int(stmt_sel, 1);
    }
    sqlite3_finalize(stmt_sel);
    unlock_db();
    
    lock_db();
    const char *sql_check = "SELECT COUNT(*) FROM Clienti WHERE clientid=?;";
    sqlite3_stmt *stmt_check;
    rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt_check, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt_check, 1, client_socket);
        if (sqlite3_step(stmt_check) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt_check, 0);
            if (count == 0) {
                unlock_db();
                register_client_in_db(client_socket);
            } else {
                unlock_db();
            }
        } else {
            unlock_db();
        }
    } else {
        unlock_db();
    }
    sqlite3_finalize(stmt_check);

    lock_db();
    const char *sql_up = "UPDATE Clienti SET NameRoad=?, LocationKm=?, Speed=? WHERE clientid=?;";
    sqlite3_stmt *stmt_up;
    rc = sqlite3_prepare_v2(db, sql_up, -1, &stmt_up, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt_up, 1, road_name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt_up, 2, km);
        sqlite3_bind_int(stmt_up, 3, speed);
        sqlite3_bind_int(stmt_up, 4, client_socket);
        sqlite3_step(stmt_up);
        sqlite3_finalize(stmt_up);
    }
    unlock_db();

    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] == client_socket) {
            is_located[i] = true;
            break;
        }
    }
    pthread_mutex_unlock(&clients_lock);

    snprintf(response, BUFFER_SIZE,
             "Locatie client inserata (drum: %s, km: %d, viteza: %d).", road_name, km, speed);

    if (crash_val >= 0) {
        int dist = abs(crash_val - km);
        if (dist <= 15) {
            strncat(response, " [ALERTA ACCIDENT] Sunteti in raza de 15km, conduceti cu prudenta!\n", BUFFER_SIZE - strlen(response) - 1);
        } else {
            strcat(response, "\n");
        }
    } else {
        strcat(response, "\n");
    }

    char local_buf[BUFFER_SIZE];
    bzero(local_buf, sizeof(local_buf));
    check_and_recommend_speed(client_socket, local_buf);
    strncat(response, local_buf, BUFFER_SIZE - strlen(response) - 1);
}

void broadcast_message(const char *msg)
{
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != -1)
            write(clients[i], msg, strlen(msg));
    }
    pthread_mutex_unlock(&clients_lock);
}

void accident_report(int client_socket, char *response)
{
    lock_db();
    const char *sql_sel = "SELECT NameRoad, LocationKm FROM Clienti WHERE clientid=?;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql_sel, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "Eroare pregatire select accident.\n");
        unlock_db();
        return;
    }
    sqlite3_bind_int(stmt, 1, client_socket);
    char theRoad[64] = "";
    int theKm = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *rd = (const char*)sqlite3_column_text(stmt, 0);
        if (rd) strncpy(theRoad, rd, sizeof(theRoad)-1);
        theKm = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);
    if (strlen(theRoad) == 0 || theKm < 0) {
        snprintf(response, BUFFER_SIZE, "Nu avem info despre drum si km in BD.\n");
        unlock_db();
        return;
    }
    const char *upd_sql = "UPDATE Drumuri SET Crashes=? WHERE Name=?;";
    sqlite3_stmt *stmt_up;
    rc = sqlite3_prepare_v2(db, upd_sql, -1, &stmt_up, NULL);
    if (rc != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "Eroare pregatire update accident.\n");
        unlock_db();
        return;
    }
    sqlite3_bind_int(stmt_up, 1, theKm);
    sqlite3_bind_text(stmt_up, 2, theRoad, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt_up);
    if (rc != SQLITE_DONE) {
        snprintf(response, BUFFER_SIZE, "Eroare update accident: %s.\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_up);
        unlock_db();
        return;
    }
    sqlite3_finalize(stmt_up);
    unlock_db();

    char global_msg[BUFFER_SIZE];
    snprintf(global_msg, sizeof(global_msg), "\n[ALERTA ACCIDENT] Accident la km %d pe drumul '%s'!\n", theKm, theRoad);
    broadcast_message(global_msg);

    update_all_clients_speed_for_road(theRoad);

    snprintf(response, BUFFER_SIZE, "Accident raportat pe drumul %s la km %d. Mesaj transmis tuturor.\n", theRoad, theKm);
}

void clear_all_crashes(char *response)
{
    lock_db();
    const char *sql = "UPDATE Drumuri SET Crashes=NULL WHERE Crashes IS NOT NULL;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            snprintf(response, BUFFER_SIZE, "Eroare la clear_all_crashes: %s.\n", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            unlock_db();
            return;
        }
        sqlite3_finalize(stmt);
    } else {
        snprintf(response, BUFFER_SIZE, "Eroare la pregatirea clear_all_crashes: %s.\n", sqlite3_errmsg(db));
        unlock_db();
        return;
    }
    unlock_db();

    char global_msg[BUFFER_SIZE];
    snprintf(global_msg, sizeof(global_msg), "\n[INFO] Toate accidentele au fost sterse din baza de date! Vitezele revin la normal.\n");
    broadcast_message(global_msg);

    snprintf(response, BUFFER_SIZE, "All crashes have been cleared.\n");

    update_all_clients_speed_for_all_roads();
}

void show_all_drums(char *response)
{
    lock_db();
    const char *sql = "SELECT Name, Type, TotalKms, NeighbourNumber FROM Drumuri;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "Eroare pregatire select drumuri: %s\n", sqlite3_errmsg(db));
        unlock_db();
        return;
    }
    strcpy(response, "Drumurile din baza de date:\n");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        const char *type = (const char *)sqlite3_column_text(stmt, 1);
        int totalKms = sqlite3_column_int(stmt, 2);
        int nbNumber = sqlite3_column_int(stmt, 3);
        char line[256];
        snprintf(line, sizeof(line), "Nume: %s | Tip: %s | Total Kms: %d | Nr. vecini: %d\n", name, type, totalKms, nbNumber);
        strncat(response, line, BUFFER_SIZE - strlen(response) - 1);
    }
    sqlite3_finalize(stmt);
    unlock_db();
}

void cmd_gas(int client_socket, char *response)
{
    lock_db();
    const char *sql_sel = "SELECT NameRoad, LocationKm, GasInfo FROM Clienti WHERE clientid=?;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql_sel, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "Eroare la pregatirea comenzii gas.\n");
        unlock_db();
        return;
    }
    sqlite3_bind_int(stmt, 1, client_socket);
    char theRoad[64] = "";
    int theKm = -1;
    int gasInfo = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *rd = (const char *)sqlite3_column_text(stmt, 0);
        if (rd) strncpy(theRoad, rd, sizeof(theRoad)-1);
        theKm = sqlite3_column_int(stmt, 1);
        gasInfo = sqlite3_column_int(stmt, 2);
    }
    sqlite3_finalize(stmt);

    const char *sql_dr = "SELECT GasStation, GasPrices FROM Drumuri WHERE Name=?;";
    sqlite3_stmt *stmt2;
    float gasStationPos = -1.0f;
    float gasPrice = 0.0f;
    rc = sqlite3_prepare_v2(db, sql_dr, -1, &stmt2, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt2, 1, theRoad, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt2) == SQLITE_ROW) {
            gasStationPos = (float)sqlite3_column_double(stmt2, 0);
            gasPrice = (float)sqlite3_column_double(stmt2, 1);
        }
    }
    sqlite3_finalize(stmt2);
    unlock_db();

    if (theKm < 0 || strlen(theRoad) == 0) {
        snprintf(response, BUFFER_SIZE, "Nu sunteti localizat corespunzator.\n");
        return;
    }
    float dist = gasStationPos - (float)theKm;
    if (dist < 0) dist = -dist;
    snprintf(response, BUFFER_SIZE, "Info GAS: Aveti %.2f km pana la benzinarie, pret combustibil=%.2f\n", dist, gasPrice);
}

void cmd_weather(int client_socket, char *response)
{
    lock_db();
    const char *sql_sel = "SELECT NameRoad FROM Clienti WHERE clientid=?;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql_sel, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "Eroare la pregatirea weather.\n");
        unlock_db();
        return;
    }
    sqlite3_bind_int(stmt, 1, client_socket);
    char theRoad[64] = "";
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *rd = (const char*)sqlite3_column_text(stmt, 0);
        if (rd) strncpy(theRoad, rd, sizeof(theRoad)-1);
    }
    sqlite3_finalize(stmt);

    const char *sql_dr = "SELECT Weather FROM Drumuri WHERE Name=?;";
    sqlite3_stmt *stmt2;
    char theWeather[32] = "";
    rc = sqlite3_prepare_v2(db, sql_dr, -1, &stmt2, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt2, 1, theRoad, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt2) == SQLITE_ROW) {
            const char *w = (const char*)sqlite3_column_text(stmt2, 0);
            if (w) strncpy(theWeather, w, sizeof(theWeather)-1);
        }
    }
    sqlite3_finalize(stmt2);
    unlock_db();

    if (strlen(theRoad) == 0) {
        snprintf(response, BUFFER_SIZE, "Nu sunteti localizat.\n");
        return;
    }
    snprintf(response, BUFFER_SIZE, "Info WEATHER: Drumul %s are vreme %s.\n", theRoad, theWeather);
}

void cmd_sports(int client_socket, char *response)
{
    int r = rand() % 5;
    snprintf(response, BUFFER_SIZE, "Info SPORTS: %s\n", sportsInfo[r]);
}

void cmd_allinfo(int client_socket, char *response)
{
    char bufGas[BUFFER_SIZE];
    char bufWea[BUFFER_SIZE];
    char bufSpo[BUFFER_SIZE];
    bzero(bufGas, sizeof(bufGas));
    bzero(bufWea, sizeof(bufWea));
    bzero(bufSpo, sizeof(bufSpo));

    cmd_gas(client_socket, bufGas);
    cmd_weather(client_socket, bufWea);
    cmd_sports(client_socket, bufSpo);

    snprintf(response, BUFFER_SIZE, "=== All Info ===\n%s%s%s", bufGas, bufWea, bufSpo);
}

void check_and_recommend_speed(int client_socket, char *response)
{
    lock_db();
    const char *sql_sel = "SELECT NameRoad, LocationKm, Speed FROM Clienti WHERE clientid=?;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql_sel, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "Eroare check_speed.\n");
        unlock_db();
        return;
    }
    sqlite3_bind_int(stmt, 1, client_socket);

    char theRoad[64] = "";
    int theKm = -1;
    int currentSpeed = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *rd = (const char*)sqlite3_column_text(stmt, 0);
        if (rd) strncpy(theRoad, rd, sizeof(theRoad)-1);
        theKm = sqlite3_column_int(stmt, 1);
        currentSpeed = sqlite3_column_int(stmt, 2);
    }
    sqlite3_finalize(stmt);

    if (strlen(theRoad) == 0 || theKm < 0) {
        snprintf(response, BUFFER_SIZE, "Nu aveti o locatie valida.\n");
        unlock_db();
        return;
    }

    int baseSpeed = 50;
    int crashPos = -1;
    const char *sql_drum = "SELECT Speed, Crashes FROM Drumuri WHERE Name=?;";
    sqlite3_stmt *stmt2;
    rc = sqlite3_prepare_v2(db, sql_drum, -1, &stmt2, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt2, 1, theRoad, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt2) == SQLITE_ROW) {
            baseSpeed = sqlite3_column_int(stmt2, 0);
            if (sqlite3_column_type(stmt2, 1) != SQLITE_NULL) {
                crashPos = sqlite3_column_int(stmt2, 1);
            }
        }
    }
    sqlite3_finalize(stmt2);
    unlock_db();

    int maxAllowedSpeed = baseSpeed;
    bool inRangeOfCrash = false;
    if (crashPos >= 0) {
        int dist = abs(crashPos - theKm);
        if (dist <= 15) {
            inRangeOfCrash = true;
            maxAllowedSpeed -= 30;
            if (maxAllowedSpeed < 0) maxAllowedSpeed = 0;
        }
    }

    char tmp[BUFFER_SIZE];
    bzero(tmp, sizeof(tmp));
    if (inRangeOfCrash) {
        snprintf(tmp, sizeof(tmp), "A fost raportat un accident pe ruta dumneavoastra, conduceti cu prudenta!\n");
    }

    if (currentSpeed > maxAllowedSpeed) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Aveti %d km/h, viteza maxima permisa este %d. Va rugam sa incetiniti!\n", currentSpeed, maxAllowedSpeed);
        strncat(tmp, buf, BUFFER_SIZE - strlen(tmp) - 1);
    }
    else if (currentSpeed < maxAllowedSpeed - 10) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Aveti %d km/h, puteti accelera in siguranta pana la %d.\n", currentSpeed, maxAllowedSpeed);
        strncat(tmp, buf, BUFFER_SIZE - strlen(tmp) - 1);
    }
    else {
        char buf[256];
        snprintf(buf, sizeof(buf), "Viteza dvs (%d) este ok (max permisa %d).\n", currentSpeed, maxAllowedSpeed);
        strncat(tmp, buf, BUFFER_SIZE - strlen(tmp) - 1);
    }

    strncpy(response, tmp, BUFFER_SIZE - 1);
}

void cmd_viteza(int client_socket, int newSpeed, char *response)
{
    lock_db();
    const char *sql_up = "UPDATE Clienti SET Speed=? WHERE clientid=?;";
    sqlite3_stmt *stmt_up;
    int rc = sqlite3_prepare_v2(db, sql_up, -1, &stmt_up, NULL);
    if (rc != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "Eroare preg. cmd_viteza.\n");
        unlock_db();
        return;
    }
    sqlite3_bind_int(stmt_up, 1, newSpeed);
    sqlite3_bind_int(stmt_up, 2, client_socket);
    sqlite3_step(stmt_up);
    sqlite3_finalize(stmt_up);
    unlock_db();

    char local_buf[BUFFER_SIZE];
    bzero(local_buf, sizeof(local_buf));
    check_and_recommend_speed(client_socket, local_buf);

    snprintf(response, BUFFER_SIZE, "Viteza actualizata la %d km/h.\n%s", newSpeed, local_buf);
}

void process_client_message(const char *message, char *response, int client_socket)
{
    int index = -1;
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] == client_socket) {
            index = i;
            break;
        }
    }
    pthread_mutex_unlock(&clients_lock);

    if (strncasecmp(message, "help", 4) == 0) {
        snprintf(response, BUFFER_SIZE,
                 "Comenzi:\n"
                 "  locatie <drum> <km> <viteza>\n"
                 "  accident\n"
                 "  viteza <valoare>\n"
                 "  insert_drum <nume> <tip>\n"
                 "  show_drums\n"
                 "  CL#ARCR4SH\n"
                 "  gas\n"
                 "  weather\n"
                 "  sports\n"
                 "  AllInfo\n"
                 "  exit\n"
                 "  help\n");
        return;
    }

    if (strncasecmp(message, "locatie", 7) != 0 &&
         strncasecmp(message, "exit", 4) != 0 &&
         strncasecmp(message, "insert_drum", 11) != 0 &&
         strncasecmp(message, "show_drums", 10) != 0 &&
         strncasecmp(message, "help", 4) != 0 &&
         !is_located[index])
    {
        snprintf(response, BUFFER_SIZE, "Nu sunteti localizat. Folositi mai intai: locatie <drum> <km> <viteza>.\n");
        return;
    }

    if (strncasecmp(message, "locatie", 7) == 0) {
        char *road = get_nth_word(message, 2);
        char *km_str = get_nth_word(message, 3);
        char *speed_str = get_nth_word(message, 4);
        if (!road || !km_str || !speed_str) {
            snprintf(response, BUFFER_SIZE, "Format invalid. Folosire: locatie <drum> <km> <viteza>\n");
            if (road) free(road);
            if (km_str) free(km_str);
            if (speed_str) free(speed_str);
            return;
        }
        char temp_resp[BUFFER_SIZE];
        bzero(temp_resp, sizeof(temp_resp));
        snprintf(temp_resp, sizeof(temp_resp), "locatie %s", road);
        check_bd_insert(temp_resp, response);

        if (strstr(response, "Nu v-am gasit locatia") != NULL) {
            free(road); free(km_str); free(speed_str);
            return;
        } else {
            int km_val = atoi(km_str);
            int spd_val = atoi(speed_str);
            bzero(response, BUFFER_SIZE);
            insert_client_location(client_socket, road, km_val, spd_val, response);
        }
        free(road); free(km_str); free(speed_str);
    }
    else if (strncasecmp(message, "accident", 8) == 0) {
        accident_report(client_socket, response);
    }
    else if (strncasecmp(message, "viteza", 6) == 0) {
        char *val_str = get_nth_word(message, 2);
        if (!val_str) {
            snprintf(response, BUFFER_SIZE, "Format: viteza <valoare>\n");
            return;
        }
        int newSpeed = atoi(val_str);
        free(val_str);
        cmd_viteza(client_socket, newSpeed, response);
    }
    else if (strncasecmp(message, "insert_drum", 11) == 0) {
        insert_bd(message, response);
    }
    else if (strncasecmp(message, "show_drums", 10) == 0) {
        show_all_drums(response);
    }
    else if (strncasecmp(message, "CL#ARCR4SH", 9) == 0) {
        clear_all_crashes(response);
    }
    else if (strncasecmp(message, "gas", 3) == 0) {
        cmd_gas(client_socket, response);
    }
    else if (strncasecmp(message, "weather", 7) == 0) {
        cmd_weather(client_socket, response);
    }
    else if (strncasecmp(message, "sports", 6) == 0) {
        cmd_sports(client_socket, response);
    }
    else if (strncasecmp(message, "AllInfo", 7) == 0) {
        cmd_allinfo(client_socket, response);
    }
    else if (strncasecmp(message, "exit", 4) == 0) {
        snprintf(response, BUFFER_SIZE, "Server is closing the connection.\n");
    }
    else {
        snprintf(response, BUFFER_SIZE, "Unknown command.\n");
    }
}

void *handle_client(void *arg)
{
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    printf("New client connected. Socket fd: %d\n", client_socket);

    register_client_in_db(client_socket);

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(client_socket, &readfds);
        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;

        int activity = select(client_socket + 1, &readfds, NULL, NULL, &tv);
        if (activity < 0) {
            perror("select error");
            break;
        }
        if (activity == 0) {
            char speed_msg[BUFFER_SIZE];
            bzero(speed_msg, sizeof(speed_msg));
            check_and_recommend_speed(client_socket, speed_msg);
            if (write(client_socket, speed_msg, strlen(speed_msg)) <= 0) {
                perror("Error writing periodic speed update");
                break;
            }
            continue;
        }
        bzero(buffer, BUFFER_SIZE);
        int valread = read(client_socket, buffer, BUFFER_SIZE - 1);
        if (valread <= 0) {
            printf("Client disconnected. Socket fd: %d\n", client_socket);
            pthread_mutex_lock(&clients_lock);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i] == client_socket) {
                    clients[i] = -1;
                    is_located[i] = false;
                    break;
                }
            }
            pthread_mutex_unlock(&clients_lock);

            unregister_client_in_db(client_socket);
            close(client_socket);
            break;
        }

        buffer[valread] = '\0';
        char response[BUFFER_SIZE];
        bzero(response, BUFFER_SIZE);

        process_client_message(buffer, response, client_socket);

        if (write(client_socket, response, strlen(response)) <= 0) {
            perror("Error writing to client");

            pthread_mutex_lock(&clients_lock);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i] == client_socket) {
                    clients[i] = -1;
                    is_located[i] = false;
                    break;
                }
            }
            pthread_mutex_unlock(&clients_lock);

            unregister_client_in_db(client_socket);
            close(client_socket);
            break;
        }

        if (strncasecmp(buffer, "exit", 4) == 0) {
            unregister_client_in_db(client_socket);
            pthread_mutex_lock(&clients_lock);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i] == client_socket) {
                    clients[i] = -1;
                    is_located[i] = false;
                    break;
                }
            }
            pthread_mutex_unlock(&clients_lock);

            close(client_socket);
            break;
        }
    }
    return NULL;
}

void update_all_clients_speed_for_road(const char *roadName)
{
    lock_db();
    const char *sql_crash = "SELECT Crashes FROM Drumuri WHERE Name=?;";
    sqlite3_stmt *stmt_crash;
    int rc = sqlite3_prepare_v2(db, sql_crash, -1, &stmt_crash, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Eroare preg update_all_clients_speed_for_road: %s\n", sqlite3_errmsg(db));
        unlock_db();
        return;
    }
    sqlite3_bind_text(stmt_crash, 1, roadName, -1, SQLITE_TRANSIENT);

    int crashPos = -1;
    if (sqlite3_step(stmt_crash) == SQLITE_ROW) {
        if (sqlite3_column_type(stmt_crash, 0) != SQLITE_NULL) {
            crashPos = sqlite3_column_int(stmt_crash, 0);
        }
    }
    sqlite3_finalize(stmt_crash);

    const char *sql = "SELECT clientid, LocationKm FROM Clienti WHERE NameRoad=?;";
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Eroare preg update_all_clients_speed_for_road: %s\n", sqlite3_errmsg(db));
        unlock_db();
        return;
    }
    sqlite3_bind_text(stmt, 1, roadName, -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int c_id = sqlite3_column_int(stmt, 0);
        int clientKm = sqlite3_column_int(stmt, 1);

        if (crashPos >= 0) {
            int dist = abs(crashPos - clientKm);
            if (dist <= 15) {
                unlock_db();
                char local_buf[BUFFER_SIZE];
                bzero(local_buf, sizeof(local_buf));
                check_and_recommend_speed(c_id, local_buf);
                pthread_mutex_lock(&clients_lock);
                write(c_id, local_buf, strlen(local_buf));
                pthread_mutex_unlock(&clients_lock);
                lock_db();
            } else {
                unlock_db();
                char local_buf[BUFFER_SIZE];
                bzero(local_buf, sizeof(local_buf));
                check_and_recommend_speed(c_id, local_buf);
                pthread_mutex_lock(&clients_lock);
                write(c_id, local_buf, strlen(local_buf));
                pthread_mutex_unlock(&clients_lock);
                lock_db();
            }
        } else {
            unlock_db();
            char local_buf[BUFFER_SIZE];
            bzero(local_buf, sizeof(local_buf));
            check_and_recommend_speed(c_id, local_buf);
            pthread_mutex_lock(&clients_lock);
            write(c_id, local_buf, strlen(local_buf));
            pthread_mutex_unlock(&clients_lock);
            lock_db();
        }
    }
    sqlite3_finalize(stmt);
    unlock_db();
}

void update_all_clients_speed_for_all_roads()
{
    lock_db();
    const char *sql = "SELECT clientid FROM Clienti;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Eroare preg update_all_clients_speed_for_all_roads: %s\n", sqlite3_errmsg(db));
        unlock_db();
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int c_id = sqlite3_column_int(stmt, 0);
        unlock_db();
        char local_buf[BUFFER_SIZE];
        bzero(local_buf, sizeof(local_buf));
        check_and_recommend_speed(c_id, local_buf);
        pthread_mutex_lock(&clients_lock);
        write(c_id, local_buf, strlen(local_buf));
        pthread_mutex_unlock(&clients_lock);
        lock_db();
    }
    sqlite3_finalize(stmt);
    unlock_db();
}
