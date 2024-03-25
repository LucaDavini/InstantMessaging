#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <errno.h>


#define BUFF_SIZE 1024      // dimensione di default del buffer utente
#define USER_DATA_SIZE 50       // dimensione limite per i dati inseriti dall'utente
#define FILENAME_SIZE 150    // come file di dimensione massima ne prevedo uno del tipo "./DEVICES/username/CHATS/username.bin"
#define MAX_GROUP_MEMBERS 10   // numero massimo di membri di un gruppo
#define TIME_SIZE 17        // gestisco i timestamp nel formato "dd/MM/yyyy hh/mm"
#define MESSAGE_SIZE 500    // definisco una dimensione massima per un messaggio

#define CHAT_MSG 0          // tipo msg: messaggio di chat standard
#define ADD_MEMBER 1        // tipo msg: richiesta di aggiunta membro alla chat
#define CHAT_REQ 2          // tipo msg: richiesta di ingresso in una chat
#define GROUP_REQ 3         // tipo msg: richiesta di ingresso in un gruppo
#define ONLINE_LIST_REQ 4   // tipo msg: richiesta di ricevere la lista degli utenti online
#define PENDING_READ 5      // tipo msg: indica che sono stati letti tutti i messaggi pendenti di una specifica chat
#define QUIT_MSG 6          // tipo msg: l'utente ha chiuso la chat. È utile per gruppi e chat offline


// definisco delle strutture globali di utilità
struct active_process {     // aiuta a tenere traccia dei processi attivi
    pid_t pid;
    struct active_process *next;
};

struct user_credentials {       //  contiene i dati che caratterizzano l'account di un utente
    char username[USER_DATA_SIZE];
    char password[USER_DATA_SIZE];
};

struct register_entry {     // definisce un singolo record del registro degli utenti registrati
    char username[USER_DATA_SIZE];
    uint16_t port;
    char timestamp_login[TIME_SIZE];
    char timestamp_logout[TIME_SIZE];
};

struct chat_message {   // definisce il formato di un messaggio di chat
    char sender[USER_DATA_SIZE];
    char chat_name[USER_DATA_SIZE];     // utile per recuperare chat di gruppo
    int type;                       // indica il tipo di messaggio
    char submission_timestamp[TIME_SIZE];
    char receipt_timestamp[TIME_SIZE];
    char message[MESSAGE_SIZE];
};

struct hanging_line {   // formato per le varie linee che costituiscono il risultato di una richiesta di hanging
    char chat_name[USER_DATA_SIZE];
    int msg_number;
    char timestamp[TIME_SIZE];
    struct hanging_line *next_line;     // per poter costruire una lista delle linee che comporranno il risultato
};

struct online_user {    // formato per la visualizzazione del comando list
    char username[USER_DATA_SIZE];
    int port;
    char timestamp_login[TIME_SIZE];
    struct online_user *next_user;  // per la costruzione della lista degli utenti online
};

struct group_info {     // definisce il singolo record del registro delle chat di gruppo
    char chat_name[USER_DATA_SIZE];
    int members_num;
    char members_list[USER_DATA_SIZE][MAX_GROUP_MEMBERS];
};


// inserisco i prototipi di tutte le funzioni costruite
void hanging(char *user, char *result);

void show(int sd, char *logged_user, char *username);
void notify_pending_read(char *sender, char *logged_user);

void chat(char *username, char *sending_user, char *result);
int send_notification(struct register_entry receiver, struct chat_message notification);
void offline_chat(int sd, char *sender, char *user_dest);
void add_member(char *parameters, char *logged_user, char *result);
void set_online_list(char *result);

char* login(char *parameters, char *logged_user);
char* signup(char *parameters, char *logged_user);
void user_logout(char *username);

void op_handler(int sd);
int cmd_handler();

int send_msg(int sd, char *buffer);
int recv_msg(int sd, char *buffer);
int send_chat_msg(int sd, struct chat_message msg);
int recv_chat_msg(int sd, struct chat_message *msg);

int is_registered(char *username);
int get_online_users(struct online_user **list_head);
void set_timestamp(char *timestamp, int type);
int socket_configurator(int port, int type);



// Funzione per la gestione di una richiesta di "hanging", che restituisce una lista delle chat da cui l'utente
// ha ricevuto dei messaggi pendenti
void hanging (char *user, char *result) {
    FILE *fptr;
    struct chat_message msg;
    char file_name[FILENAME_SIZE];

    struct hanging_line *list_head = NULL;  // puntatore alla testa della lista
    struct hanging_line *list_scanner;      // puntatore utile a scorrere la lista
    int registered = 0;     // indica se la chat è già stata registrata o meno

    char formatted_line[BUFF_SIZE];     // conterrà una singola linea formattata per la costruzione del messaggio finale


    sprintf(file_name, "./SERVER/PENDING/%s.bin", user);
    fptr = fopen(file_name, "rb");

    if (fptr != NULL) {
        while (1) {
            fread(&msg, sizeof(struct chat_message), 1, fptr);

            if (feof(fptr)) break;


            if (strcmp(msg.receipt_timestamp, "") == 0) {   // messaggio non ancora ricevuto
                if (list_head == NULL) {    // primo inserimento nella lista
                    list_head = malloc(sizeof(struct hanging_line));    // alloco memoria dinamicamente per la nuova linea

                    if (list_head == NULL) {
                        printf("%s\n", "Impossibile allocare ulteriore memoria");
                        strcpy(result, "Impossibile recuperare la lista dei messaggi delle chat pendenti"); // messaggio per il client

                        return;
                    }

                    strcpy(list_head->chat_name, msg.chat_name);
                    list_head->msg_number = 1;
                    strcpy(list_head->timestamp, msg.submission_timestamp);
                    list_head->next_line = NULL;
                }
                else {
                    list_scanner = list_head;

                    while (list_scanner != NULL) {
                        if (strcmp(list_scanner->chat_name, msg.chat_name) == 0) {  // se la chat è già stata segnata, incremento il contatore del numero di messaggi ricevuti
                            list_scanner->msg_number += 1;
                            strcpy(list_scanner->timestamp, msg.submission_timestamp);

                            registered = 1;
                            break;
                        }

                        list_scanner = list_scanner->next_line;
                    }


                    if (!registered) {      // se non è ancora stata segnata, inserisco in cima alla lista, dato che è probabile riavere un messaggio dello stesso utente al prossimo giro
                        list_scanner = malloc(sizeof(struct hanging_line));

                        if (list_head == NULL) {
                            printf("%s\n", "Impossibile allocare ulteriore memoria");
                            strcpy(result, "Impossibile recuperare la lista dei messaggi delle chat pendenti");

                            return;
                        }

                        strcpy(list_scanner->chat_name, msg.chat_name);
                        list_scanner->msg_number = 1;
                        strcpy(list_scanner->timestamp, msg.submission_timestamp);
                        list_scanner->next_line = list_head;

                        list_head = list_scanner;
                    }
                    else {
                        registered = 0;     // resetto la variabile per il prossimo messaggio
                    }
                }
            }
        }

        fclose(fptr);


        if (list_head == NULL) {
            strcpy(result, "Nessun messaggio ricevuto\n");
            return;
        }

        while (list_head != NULL) {     // scorro la lista creata per costruire il messaggio da inviare al client
            sprintf(formatted_line, "%s (%d)  %s\n", list_head->chat_name, list_head->msg_number, list_head->timestamp);
            strcat(result, formatted_line);

            list_head = list_head->next_line;
        }
    }
    else {
        strcpy(result, "Nessun messaggio ricevuto\n");
    }
}


// Funzione per le gestione del comando "show" che recupera i messaggi pendenti e li invia uno ad uno all'utente
// richiedente. Notifica all'utente che ha inviato i messaggi la presa visione se questo è online, altrimenti
// memorizza il messaggio di notifica come messaggio pendente
void show (int sd, char *logged_user, char *username) {
    FILE *fptr;
    char file_name[FILENAME_SIZE];

    struct chat_message msg;
    int msg_num = 0;    // indica il numero di messaggi pendenti

    int ret;


    sprintf(file_name, "./SERVER/PENDING/%s.bin", logged_user);
    fptr = fopen(file_name, "rb+");     // apro il file dei messaggi pendenti dell'utente

    if (fptr != NULL) {
        while (1) {
            fread(&msg, sizeof(struct chat_message), 1, fptr);

            if (feof(fptr)) break;

            if (strcmp(msg.receipt_timestamp, "") == 0 &&
                strcmp(msg.sender, username) == 0) {    // trovato un messaggio pendente ricevuto dall'utente indicato

                set_timestamp(msg.receipt_timestamp, 1);    // aggiorno il timestamp di ricezione al timestamp corrente

                ret = send_chat_msg(sd, msg);     // invio il messaggio all'utente

                if (ret < 0) {
                    close(sd);

                    perror("Errore in fase di invio messaggi pendenti");
                    printf("%s", "\n");

                    exit(-1);
                }


                fseek(fptr, -sizeof(struct chat_message), SEEK_CUR);
                fwrite(&msg, sizeof(struct chat_message), 1, fptr);     // aggiorno il log dei messaggi pendenti, in modo che quel messaggio non sia più considerato tale

                if (msg.type == CHAT_MSG) {
                    msg_num++;
                }
            }
        }


        fclose(fptr);
    }


    msg.type = QUIT_MSG;
    ret = send_chat_msg(sd, msg);     // invio il messaggio che indica la fine della lista dei messaggi pendenti

    if (ret < 0) {
        close(sd);

        perror("Errore in fase di invio messaggi pendenti");
        printf("%s", "\n");

        exit(-1);
    }

    if (msg_num > 0) {
        notify_pending_read(username, logged_user);      // notifico al mittente dei messaggi che l'utente li ha letti
    }
}


// Funzione che si occupa di notificare all'utente mittente di messaggi pendenti, che il destinatario dei tali
// ne ha preso visione. Se dovesse risultare offline, memorizzo la notifica tra i suoi messaggi pendenti
void notify_pending_read (char *sender, char *logged_user) {
    FILE *fptr;
    char file_name[FILENAME_SIZE];
    struct register_entry user_data;    // per verificare se il mittente è online o meno
    
    struct chat_message notification;    // conterrà il messaggio di notifica


    fptr = fopen("./SERVER/users_register.bin", "rb");

    while (1) {     // cerco l'utente mittente
        fread(&user_data, sizeof(struct register_entry), 1, fptr);
        
        if (feof(fptr)) {
            return;
        }

        if (strcmp(user_data.username, sender) == 0) {  // utente mittente trovato
            break;
        }
    }

    fclose(fptr);


    strcpy(notification.sender, logged_user);   // costruisco il messaggio di notifica di avvenuta ricezione
    strcpy(notification.chat_name, logged_user);
    notification.type = PENDING_READ;
    set_timestamp(notification.submission_timestamp, 1);
    set_timestamp(notification.receipt_timestamp, 0);       // considero il messaggio come non ricevuto in modo che sia leggibile dal file pending
    strcpy(notification.message, "");


    if (strcmp(user_data.timestamp_logout, "") == 0) {  // l'utente mittente risulta online
        if (send_notification(user_data, notification)) {   // provo a inviargli la notifica
            return;
        }
    }

    sprintf(file_name, "./SERVER/PENDING/%s.bin", sender);  // se risulta offline o la "send_notification" fallisce, salvo la notifica tra i suoi messaggi pendenti
    fptr = fopen(file_name, "ab");

    fwrite(&notification, sizeof(struct chat_message), 1, fptr);

    fclose(fptr);
}


// Funzione per la gestione di una richiesta di "chat" che memorizza il risultato nel parametro "result"
void chat (char *username, char *sending_user, char *result) {
    FILE *fptr;
    struct register_entry user_data;    // per recuperare i dati dell'utente da contattare
    struct chat_message notification;   // messaggio di notifica da inviare all'utente da contattare


    fptr = fopen("./SERVER/users_register.bin", "rb");

    while (1) {     // controllo che l'utente con cui si vuole aprire una chat esista
        fread(&user_data, sizeof(struct register_entry), 1, fptr);

        if (feof(fptr)) {   // utente non registrato
            fclose(fptr);
            strcpy(result, "Utente inesistente");
            
            return;
        }

        if (strcmp(user_data.username, username) == 0) {    // utente trovato
            fclose(fptr);

            break;
        }
    }


    if (strcmp(user_data.timestamp_logout, "") == 0) {      // se l'utente è online costruisco la richiesta di partecipazione a una chat da inviargli
        strcpy(notification.sender, sending_user);
        strcpy(notification.chat_name, sending_user);
        notification.type = CHAT_REQ;
        set_timestamp(notification.submission_timestamp, 1);
        set_timestamp(notification.receipt_timestamp, 1);
        strcpy(notification.message, "");   // il contenuto di questo messaggio è irrilevante

        if (send_notification(user_data, notification)) {  // provo a mandare la richiesta all'utente
            sprintf(result, "%d", user_data.port);  // sse riesco a inviare la richiesta, ritorno al richiedente la porta su cui contattarlo
            return;
        }
    }

    strcpy(result, "Utente offline");
}


// Funzione che per 3 volte tenta di inviare una notifica all'utente indicato.
// Se l'utente non fosse contattabile, viene considerato offline e la funzione ritorna 0
int send_notification (struct register_entry receiver, struct chat_message notification) {
    int dest_sd;    // socket creato per notificare la richiesta di apertura di chat all'utente destinatario
    int i;  // variabile per il ciclo for
    int ret;    // per controllare i risultati delle operazioni

    FILE *fptr;     // per aggiornare il registro nel caso l'utente non risulti contattabile
    struct register_entry user;


    for (i = 0; i < 3; i++) {   // creo il socket di comunicazione. In caso di errori provo nuovamente
        dest_sd = socket_configurator(receiver.port, 1);

        if (dest_sd > 0) break;
    }

    if (i < 3) {
        for (i = 0; i < 3; i++) {   // tento di inviare il messaggio di richiesta per 3 volte
            ret = send_chat_msg(dest_sd, notification);

            if (ret > 0) break;
        }

        close(dest_sd);     // non serve più un canale aperto con l'utente destinatario

        if (i < 3) {    // operazione riuscita
            return 1;
        }
    }


    printf("Notifica non inoltrata: %s non è raggiungibile.\n\n", receiver.username);     // se non sono riuscito a contattare l'utente, stampo un messaggio di errore

    fptr = fopen("./SERVER/users_register.bin", "rb+");     // aggiorno lo stato dell'utente a offline nel file "users_register"

    if (fptr != NULL) {
        while (1) {
            fread(&user, sizeof(struct register_entry), 1, fptr);

            if (feof(fptr)) break;

            if (strcmp(user.username, receiver.username) == 0) {
                set_timestamp(user.timestamp_logout, 1);      // imposto il timestamp di logout al timestamp corrente

                fseek(fptr, -sizeof(struct register_entry), SEEK_CUR);
                fwrite(&user, sizeof(struct register_entry), 1, fptr);

                break;
            }
        }

        fclose(fptr);
    }

    return 0;
}


// Funzione per la ricezione di messaggi pendenti da parte di un utente il cui interlocutore risulta online.
// I messaggi ricevuti sono salvati sul file dei messaggi pendenti dell'utente destinatario
void offline_chat (int sd, char *sender, char *user_dest) {
    struct chat_message msg;
    int ret;

    char formatted_list[BUFF_SIZE];     // per contenere la lista di utenti online nel formato da inviare all'utente in caso di messaggio ONLINE_LIST_REQ

    FILE *fptr;
    char file_name[FILENAME_SIZE];


    while (1) {
        ret = recv_chat_msg(sd, &msg);

        if (ret <= 0) {
            close(sd);

            if (ret == 0 || errno == 104) {     // gestisco errno 104 come ret == 0, poiché si tratta di chiusura canale da parte dell'utente
                user_logout(sender);
                
                exit(0);
            }
            else {
                perror("Errore in ricezione messaggio");
                printf("%s", "\n");
                
                exit(-1);
            }
        }


        if (msg.type == QUIT_MSG) {     // tipo di messaggio ricevuto quando l'utente preme \q per uscire dalla chat
            return;
        }
        else if (msg.type == ONLINE_LIST_REQ) {  // tipo di messaggio ricevuto quando l'utente richiede la lista degli utenti connessi
            set_online_list(formatted_list);

            ret = send_msg(sd, formatted_list);

            if (ret < 0) {
                close(sd);

                if (errno == 104) {     // errno 104 indica chiusura canale da parte dell'utente
                    user_logout(sender);
                    
                    exit(0);
                }
                else {
                    perror("Errore in invio lista utenti connessi");
                    printf("%s", "\n");
                    
                    exit(-1);
                }
            }

            continue;   // non memorizzo il messaggio e continuo la chat offline
        }


        sprintf(file_name, "./SERVER/PENDING/%s.bin", user_dest);
        fptr = fopen(file_name, "ab");      // inserisco il nuovo messaggio pendente in fondo al file

        fwrite(&msg, sizeof(struct chat_message), 1, fptr);

        fclose(fptr);
        
        printf("Salvato messaggio di %s per %s\n", sender, user_dest);
    }
}


// Funzione che si occupa della richiesta di aggiunta membro a una chat. Recupera il nome della chat di gruppo
// o ne crea uno nuovo 
void add_member (char *parameters, char *logged_user, char *result) {
    char new_member_name[USER_DATA_SIZE];
    char current_chat[USER_DATA_SIZE];
    int current_members_num;

    FILE *fptr;
    struct register_entry new_member_data;  // per controllare se l'utente è online
    struct group_info group;    // per recuperare le informazioni sul gruppo corrente

    struct group_info existing_group;    // per controllare se il nuovo gruppo esiste già
    int group_id = 0;   // contatore utilizzato come id dei nuovi gruppi
    int found = 0;      // per controllare i membri dei gruppi
    int i, j;  // per i cicli

    struct chat_message notification;   // per la notifica di richiesta partecipazione a un gruppo per il nuovo utente
    int ret;


    strcpy(new_member_name, strtok_r(parameters, " ", &parameters));     // recupero i parametri dal comando dell'utente
    strcpy(current_chat, strtok_r(parameters, " ", &parameters));
    current_members_num = atoi(parameters);

    
    fptr = fopen("./SERVER/users_register.bin", "rb");  // sono sicuro che il file esista, quindi non effettuo controlli al riguardo

    while (1) {     // controllo se l'utente è online e nel caso recupero le sue informazioni
        fread(&new_member_data, sizeof(struct register_entry), 1, fptr);

        if (feof(fptr)) {
            strcpy(result, "Utente inesistente");
            return;
        }

        if (strcmp(new_member_data.username, new_member_name) == 0) {
            if (strcmp(new_member_data.timestamp_logout, "") != 0) {
                strcpy(result, "Utente offline");
                return;
            }
            else {
                break;      // utente online recuperato, esco dal while
            }
        }
    }

    fclose(fptr);


    if (current_members_num > 2) {  // se la chat corrente è un gruppo, recupero le informazioni dal registro delle chat di gruppo
        fptr = fopen("./SERVER/groups_register.bin", "rb");

        if (fptr != NULL) {
            while (1) {
                fread(&group, sizeof(struct group_info), 1, fptr);

                if (feof(fptr)) {
                    strcpy(result, "Impossibile completare l'operazione");  // la chat corrente è un gruppo ma non è registrata
                    return;
                }

                if (strcmp(group.chat_name, current_chat) == 0) {   // trovato il gruppo corrente
                    for (i = 0; i < group.members_num; i++) {
                        if (strcmp(group.members_list[i], new_member_name) == 0) {  // prima di procedere, controllo che il nuovo membro non faccia già parte del gruppo
                            strcpy(result, "L'utente fa già parte della chat");
                            return;
                        }
                    }

                    break;
                }
            }

            fclose(fptr);
        }
        else {
            strcpy(result, "Impossibile completare l'operazione");
            return;
        }
    }
    else {  // se non è un gruppo, inserisco i primi due membri nel group_info
        strcpy(group.members_list[0], logged_user);
        strcpy(group.members_list[1], current_chat);    // è il nome dell'altro membro partecipante alla chat corrente di "logged_user"
    }

    strcpy(group.members_list[current_members_num], new_member_name);   // aggiungo le informazioni sul nuovo gruppo
    group.members_num = current_members_num + 1;


    fptr = fopen("./SERVER/groups_register.bin", "ab+");

    while (1) {
        fread(&existing_group, sizeof(struct group_info), 1, fptr);

        group_id++;     // conto il numero dei gruppi registrati per dare l'id ad un eventuale nuovo gruppo

        if (feof(fptr)) break;

        if (group.members_num == existing_group.members_num) {  // se hanno lo stesso numero di partecipanti, controllo che siano gli stessi partecipanti
            for (i = 0; i < group.members_num; i++) {
                found = 0;

                for (j = 0; j < group.members_num; j++) {
                    if (strcmp(group.members_list[i], existing_group.members_list[j]) == 0) {   // il partecipante è presente anche nel gruppo registrato
                        found = 1;
                        break;
                    }
                }

                if (found == 0) {   // manca uno dei partecipanti, quindi non sono lo stesso gruppo
                    break;
                }
            }

            if (found) {    // se i due gruppi hanno gli stessi partecipanti, significa che il nuovo gruppo era già registrato, quindi ne recupero il nome
                strcpy(group.chat_name, existing_group.chat_name);
                break;
            }
        }
    }

    
    if (!found) {   // le informazioni sul nuovo gruppo non sono ancora state registrate 
        sprintf(group.chat_name, "group%d", group_id);  // costruisco il nome del gruppo

        fwrite(&group, sizeof(struct group_info), 1, fptr);     // memorizzo il nuovo gruppo nel registro
    }

    fclose(fptr);


    strcpy(notification.sender, logged_user);   // costruisco la notifica di richiesta gruppo per il nuovo utente
    strcpy(notification.chat_name, group.chat_name);
    notification.type = GROUP_REQ;
    set_timestamp(notification.submission_timestamp, 1);
    set_timestamp(notification.receipt_timestamp, 1);
    sprintf(notification.message, "%d", group.members_num);     // indico al nuovo membro il numero di partecipanti al gruppo
    
    ret = send_notification(new_member_data, notification);


    if (ret == 0) {     // utente offline
        strcpy(result, "Utente offline");
    }
    else {  // la notifica è stata recapitata all'utente, quindi ninvio al mittente il nome del gruppo e la posta su cui contattare il nuovo membro
        sprintf(result, "%s %d", group.chat_name, new_member_data.port);
    }
}


// Funzione che inserisce in "result" la lista degli utenti al momento connessi al servizio
void set_online_list (char *result) {
    struct online_user *list_head = NULL;


    if (get_online_users(&list_head)) {
        while (list_head != NULL) {
            sprintf(result, "%s %s", result, list_head->username);  // costruisco la stringa da inviare all'utente

            list_head = list_head->next_user;
        }
    }
    else {
        strcpy(result, "Nessun utente connesso al momento");     // se non esiste una lista, rispondo con un messaggio di default
    }
}


// Funzione per la gestione di una richiesta di "in" che restituisce il risultato dell'operazione.
// Riceve i parametri richiesti per l'operazione e un buffer in cui salvare il nome dell'utente loggato.
char* login (char *parameters, char *logged_user) {
    FILE *fptr;     // puntatore per scorrere i record del file
    struct user_credentials account;    // per controllare validità dei dati inseriti
    struct register_entry user_data;    // per controllare e aggiornare i dati del client
    char *username, *password, *port;
    int found = 0;     // indica se ho trovato o meno il record (utente) che cercavo


    username = strtok_r(parameters, " ", &parameters);  // separo i dati ricevuti dal client
    password = strtok_r(parameters, " ", &port);


    fptr = fopen("./SERVER/credentials.bin", "rb");    // apro il file "credentials" per controllare i dati dell'utente che ha richiesto il login

    if (fptr == NULL) {     // se la fopen fallisce significa che non è ancora stato registrato nessun account
        return "Utente non registrato";
    }

    while (1) {   // scorro tutti gli account registrati
        fread(&account, sizeof(struct user_credentials), 1, fptr);

        if (feof(fptr)) break;  // ho raggiunto la fine del file

        if (strcmp(account.username, username) == 0) {      // ho trovato l'utente che cercavo
            found = 1;
            break;
        }
    }

    fclose(fptr);

    if (!found) {   // fallisce se non ho trovato l'utente
        return "Utente non registrato";
    }
    else if (strcmp(account.password, password) != 0) {     // o se la password non corrisponde
        return "Password errata";
    }


    fptr = fopen("./SERVER/users_register.bin", "rb+");

    while (1) {   // scorro tutti i record degli utenti
        fread(&user_data, sizeof(struct register_entry), 1, fptr);

        if (feof(fptr)) break;  // ho raggiunto la fine del file

        if (strcmp(user_data.username, username) == 0) {      // ho trovato l'utente che cercavo
            break;
        }
    }

    user_data.port = atoi(port);    // aggiorno il record con i nuovi dati ricevuti dall'utente richiedente
    set_timestamp(user_data.timestamp_login, 1);
    set_timestamp(user_data.timestamp_logout, 0);

    fseek(fptr, -sizeof(struct register_entry), SEEK_CUR);  // torno sul record appena letto
    fwrite(&user_data, sizeof(struct register_entry), 1, fptr);     // aggiorno il record del file

    strcpy(logged_user, username);  // memorizzo lo username dell'utente una volta loggato con successo

    fclose(fptr);


    return "Successo";
}


// Funzione per la gestione di una richiesta di "signup" che restituisce il risultato dell'operazione.
// Riceve i parametri richiesti per l'operazione e un buffer in cui salvare il nome dell'utente loggato.
char* signup (char *parameters, char *logged_user) {
    FILE *fptr;     // puntatore per scorrere i record del file
    struct user_credentials account;    // unità di lettura e scrittura per il file "credentials"
    struct register_entry user_data;    // unità di lettura e scrittura per il file "users_register"
    char *username, *password, *port;


    username = strtok_r(parameters, " ", &parameters);    // recupero i dati dell'utente
    password = strtok_r(parameters, " ", &port);


    fptr = fopen("./SERVER/credentials.bin", "ab+");        // apro il file anche in scrittura per crearlo nel caso non esista ancora

    while (1) {   // scorro tutti gli account registrati del file
        fread(&account, sizeof(struct user_credentials), 1, fptr);

        if (feof(fptr)) break;  // ho raggiunto la fine del file

        if (strcmp(account.username, username) == 0) {      // impedisco la creazione di account con lo stesso username
            fclose(fptr);

            return "Utente già registrato";
        }
    }

    strcpy(account.username, username);     // memorizzo nella struttura da salvare i dati dell'utente
    strcpy(account.password, password);

    fwrite(&account, sizeof(struct user_credentials), 1, fptr);     // memorizzo il nuovo utente in fondo al file

    fclose(fptr);


    fptr = fopen("./SERVER/users_register.bin", "ab");     // riutilizzo lo stesso puntatore per aprire il file "users_register" per memorizzare le informazioni sul nuovo utente

    strcpy(user_data.username, username);   // memorizzo nella struttura da salvare i dati dell'utente
    user_data.port = atoi(port);
    set_timestamp(user_data.timestamp_login, 1);
    set_timestamp(user_data.timestamp_logout, 0);
    
    fseek(fptr, 0, SEEK_END);   // sposto il puntatore in fondo al file
    fwrite(&user_data, sizeof(struct register_entry), 1, fptr);

    strcpy(logged_user, username);  // memorizzo lo username dell'utente una volta loggato con successo

    fclose(fptr);


    return "Successo";
}


// Funzione che si occupa di aggiornare i registri quando un utente effettua il logout
void user_logout (char *username) {
    FILE *fptr;
    struct register_entry user;


    fptr = fopen("./SERVER/users_register.bin", "rb+");

    while (1) {
        fread(&user, sizeof(struct register_entry), 1, fptr);

        if (feof(fptr)) return;

        if (strcmp(user.username, username) == 0) {     // trovato il record da aggiornare
            break;
        }
    }

    
    set_timestamp(user.timestamp_logout, 1);     // aggiorno il timestamp di logout al timestamp corrente

    fseek(fptr, -sizeof(struct register_entry), SEEK_CUR);  // riposiziono il puntatore alla struttura appena letta
    fwrite(&user, sizeof(struct register_entry), 1, fptr);

    fclose(fptr);

    printf("%s si è disconnesso\n", username);
}


// Funzione per la gestione delle richieste dei client.
// Non differenzia pre-login e post-login, poiché questo viene già gestito lato client.
void op_handler (int sd) {
    char buffer[BUFF_SIZE];
    int ret;    // per controllare i valori di ritorno

    char *operation, *parameters;   // per separare l'operazione richiesta dai parametri ricevuti come messaggio di un client
    char logged_user[USER_DATA_SIZE];     // per memorizzare lo username dell'utente loggato
    char result[BUFF_SIZE];   // conterrà un messaggio che indica il risultato dell'operazione


    while (1) {    // comunico con il client finché non viene richiesto di terminare il server
        memset(&buffer, 0, BUFF_SIZE);  // pulizia memoria
        memset(&result, 0, BUFF_SIZE);


        ret = recv_msg(sd, buffer);   // attendo un comando dal client

        if (ret <= 0) {
            close(sd);

            if (ret == 0 || errno == 104) {     // errno 104 viene con ret == -1, anche se si tratta di "connection reset by peer", per questo lo tratto come un ret == 0
                user_logout(logged_user);

                return;
            }
            else {
                perror("Errore in fase di ricezione di richiesta operazione");
                printf("%s", "\n");

                exit(-1);
            }
        }


        operation = strtok_r(buffer, " ", &parameters);     // separo l'operazione per poterne discriminare la gestione

        if (strcmp(operation, "signup") == 0) {
            printf("Richiesta '%s': ", operation);

            strcpy(result, signup(parameters, logged_user));     // gestisco l'operazione e salvo il risultato in "result"

            if (strcmp(result, "Successo") == 0) {
                printf("%s si è connesso.\n", logged_user);
            }
            else {
                printf("%s\n", "fallita.");
            }
        }
        else if (strcmp(operation, "in") == 0) {
            printf("Richiesta '%s': ", operation);

            strcpy(result, login(parameters, logged_user));     // gestisco l'operazione e salvo il risultato in "result"

            if (strcmp(result, "Successo") == 0) {
                printf("%s si è connesso.\n", logged_user);
            }
            else {
                printf("%s\n", "fallita.");
            }
        }
        else if (strcmp(operation, "hanging") == 0) {
            printf("Richiesta '%s' da %s\n", operation, logged_user);

            hanging(logged_user, result);
        }
        else if (strcmp(operation, "show") == 0) {
            printf("Richiesta '%s' da %s\n", operation, logged_user);

            show(sd, logged_user, parameters);

            continue;   // la show non utilizza il messaggio di risposta e deve quindi saltare quel passaggio
        }
        else if (strcmp(operation, "chat") == 0) {
            printf("Richiesta '%s' da %s\n", operation, logged_user);

            chat(parameters, logged_user, result);
        }
        else if (strcmp(operation, "check") == 0) {
            printf("Richiesta '%s' da %s\n", operation, logged_user);

            if (is_registered(parameters)) {    // controllo se l'utente ricercato è registrato
                strcpy(result, "Utente esistente");
            }
            else {
                strcpy(result, "Utente inesistente");
            }
        }
        else if (strcmp(operation, "list") == 0) {
            printf("Richiesta '%s' da %s\n", operation, logged_user);

            set_online_list(result);
        }
        else if (strcmp(operation, "add") == 0) {
            printf("Richiesta '%s' da %s\n", operation, logged_user);

            add_member(parameters, logged_user, result);
        }
        else {
            printf("Operazione richiesta da %s non riconosciuta\n", logged_user);
            strcpy(result, "Operazione non riconosciuta");
        }


        ret = send_msg(sd, result);      // invio al client un messaggio di risposta

        if (ret < 0) {
            close(sd);

            perror("Errore in fase di invio risultato operazione");
            printf("%s", "\n");

            exit(-1);
        }


        if (strcmp(result, "Utente non registrato") == 0 ||
            strcmp(result, "Password errata") == 0) {   // in caso di "in" fallita, termino il processo
            close(sd);

            return;
        }
        else if (strcmp(result, "Utente offline") == 0) {   // se l'utente destinatario è offline, il server si occupa di mantenere eventuali messaggi pendenti
            offline_chat(sd, logged_user, parameters);      // in questo caso "parameters" contiene lo username dell'utente destinatario
        }
    }
}


// Funzione per la gestione dei comandi dell'utente lato server
int cmd_handler () {
    char cmd[BUFF_SIZE];    // buffer per raccogliere i comandi dell'utente

    struct online_user *online_users_list = NULL;  // puntatore alla testa della lista per il comando "list"


    scanf("%s", cmd);

    if (strcmp(cmd, "help") == 0) {
        system("clear");

        printf("%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
            "Lista comandi:\n",
            "help",
            "Mostra una breve descrizione dei comandi.\n",
            "list",
            "Mostra l’elenco degli utenti connessi alla rete, indicando username, timestamp di connessione e numero di porta nel formato “username*timestamp*porta”.\n",
            "esc",
            "Termina il server. La terminazione del server non impedisce alle chat in corso di proseguire. Se il server è disconnesso, nessun utente può più fare login. Gli utenti online, qualora decidano di uscire, salvano l’istante di disconnessione. Quando il server si riconnette, gli utenti che si riconnettono dopo essersi disconnessi a server disattivo inviano l’istante di disconnessione salvato.\n"
        );
    }
    else if (strcmp(cmd, "list") == 0) {
        system("clear");

        printf("Utenti connessi:\n");

        if (get_online_users(&online_users_list)) {   // se ci sono utenti online, li stampo a video
            while (online_users_list != NULL) {
                printf("%s * %s * %d\n", online_users_list->username, online_users_list->timestamp_login, online_users_list->port);   // stampo ogni record nel formato richiesto

                online_users_list = online_users_list->next_user;
            }

            printf("%s", "\n");
        }
        else {
            printf("%s\n", "Nessun utente connesso al momento\n");
        }
    }
    else if (strcmp(cmd, "esc") == 0) {
        printf("%s\n", "Terminazione server");
        return 1;
    }
    else {
        printf("%s\n", "Comando non riconosciuto. Digita help per visualizzare le operazioni possibili\n");
    }

    return 0;
}


// Funzione per l'invio di un messaggio in rete
// Il protocollo di invio richiede di notificare al destinatario la dimensione del messaggio prima di spedirlo
int send_msg (int sd, char *buffer) {
    uint16_t msg_len;   // per recuperare la dimensione del messaggio da ricevere
    int ret;    // per controllare i valori di ritorno


    msg_len = htons(strlen(buffer) + 1);

    ret = send(sd, &msg_len, sizeof(uint16_t), 0);      // invio per prima la dimensione del messaggio di risposta

    if (ret < 0) return ret;
        
    ret = send(sd, buffer, ntohs(msg_len), 0);       // invio il messaggio effettivo

    return ret;
}


// Funzione per la ricezione di un messaggio da remoto
// Il protocollo di ricezione prevede una doppia lettura: prima la dimensione del messaggio da ricevere e poi
// il messaggio effettivo
int recv_msg (int sd, char *buffer) {
    uint16_t msg_len;   // per recuperare la dimensione del messaggio da ricevere
    int ret;    // per controllare i valori di ritorno


    ret = recv(sd, &msg_len, sizeof(uint16_t), 0);   // attendo prima di ricevere la dimensione del messaggio per la successiva recv

    if (ret <= 0) return ret;

    ret = recv(sd, buffer, ntohs(msg_len), 0);

    return ret;
}


// Funzione per l'invio di un messaggio di chat
// Il protocollo prevede l'invio dei vari elementi che costituiscono un messaggio, secondo il formato definito
int send_chat_msg(int sd, struct chat_message msg) {
    int ret;


    ret = send(sd, msg.sender, USER_DATA_SIZE, 0);  // invio lo username del mittente

    if (ret < 0) return ret;

    ret = send(sd, msg.chat_name, USER_DATA_SIZE, 0);  // invio il nome della chat

    if (ret < 0) return ret;

    msg.type = htonl(msg.type); // converto 4B in network order
    ret = send(sd, &msg.type, sizeof(int), 0);  // invio il tipo di messaggio

    if (ret < 0) return ret;

    ret = send(sd, msg.submission_timestamp, TIME_SIZE, 0);  // invio il timestamp di invio

    if (ret < 0) return ret;

    ret = send(sd, msg.receipt_timestamp, TIME_SIZE, 0);  // invio il timestamp di ricezione

    if (ret < 0) return ret;


    ret = send(sd, msg.message, MESSAGE_SIZE, 0);  // invio il contenuto del messaggio


    return ret;
}


// Funzione per la ricezione di un messaggio di una chat
// Il protocollo di ricezione in questo caso si compone di una serie di recv,
// conformemente al formato dei messaggi di chat
int recv_chat_msg (int sd, struct chat_message *msg) {
    int ret;


    ret = recv(sd, msg->sender, USER_DATA_SIZE, 0);

    if (ret <= 0) return ret;

    ret = recv(sd, msg->chat_name, USER_DATA_SIZE, 0);

    if (ret <= 0) return ret;

    ret = recv(sd, &msg->type, sizeof(int), 0);

    if (ret <= 0) return ret;
    else msg->type = ntohl(msg->type);  // riconverto in host order

    ret = recv(sd, msg->submission_timestamp, TIME_SIZE, 0);

    if (ret <= 0) return ret;

    ret = recv(sd, msg->receipt_timestamp, TIME_SIZE, 0);

    if (ret <= 0) return ret;

    ret = recv(sd, msg->message, MESSAGE_SIZE, 0);


    return ret;
}


// Funzione per il controllo dell'esistenza di un utente tra gli account registrati
int is_registered (char *username) {
    FILE *fptr;
    struct user_credentials account;


    fptr = fopen("./SERVER/credentials.bin", "rb");

    while (1) {
        fread(&account, sizeof(struct user_credentials), 1, fptr);

        if (feof(fptr)) break;

        if (strcmp(account.username, username) == 0) {
            fclose(fptr);
            return 1;   // utente registrato
        }
    }

    fclose(fptr);
    return 0;   // utente non registrato
}


// Funzione che si occupa di costruire la lista degli utenti connessi al servizio.
// Ritorna 0 se questo non è stato possibile.
int get_online_users (struct online_user **list_head) {
    FILE *fptr;
    struct register_entry user;     // per la lettura

    struct online_user *new_record;   // per scorrere la lista
    int online_users = 0;   // indica il numero di utenti connessi


    fptr = fopen("./SERVER/users_register.bin", "rb");

    if (fptr == NULL) {     // non ci sono ancora utenti registrati
        return 0;
    }

    while (1) {   // scorro tutti gli utenti registrati
        fread(&user, sizeof(struct register_entry), 1, fptr);

        if (feof(fptr)) break;

        if (strcmp(user.timestamp_logout, "") == 0) {   // utente online
            new_record = malloc(sizeof(struct online_user));    // alloco memoria dinamica

            if (new_record == NULL) {
                printf("%s\n", "Impossibile allocare ulteriore memoria");
                return 0;
            }

            strcpy(new_record->username, user.username);
            new_record->port = user.port;
            strcpy(new_record->timestamp_login, user.timestamp_login);
            new_record->next_user = *list_head;

            *list_head = new_record;    // inserisco in testa per comodità, dato che l'ordine non è importante
            

            online_users++;     // anziché controllare se ci sono o meno, determino direttamente il numero degli utenti connessi
        }
    }

    fclose(fptr);

    if (online_users == 0) {
        return 0;
    }
    
    return 1;
}


// Funzione che restituisce una stringa contenente un timestamp nel formato "dd/MM/yyyy hh/mm".
// Imposta il timestamp corrente se type == 1, mentre stringa vuota se type == 0.
void set_timestamp (char *timestamp, int type) {
    time_t raw_time;
    struct tm* time_info;   // per recuperare parti del timestamp


    if (type == 1) {     // per type == 1, imposto il timestamp corrente
        raw_time = time(NULL);

        time_info = localtime(&raw_time);   // imposto un formato al timestamp da visualizzare
        sprintf(timestamp, "%d/%d/%d %d:%d", time_info->tm_mday, time_info->tm_mon + 1, time_info->tm_year + 1900, time_info->tm_hour, time_info->tm_min);
    }
    else {  // per type == 0, considero il timestamp vuoto
        strcpy(timestamp, "");
    }
}


// Funzione per la creazione e configurazione di un socket TCP.
// Il parametro type indica se di ascolto (0) o comunicazione (1))
int socket_configurator (int port, int type) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(struct sockaddr_in);
    int sd, ret;


    sd = socket(AF_INET, SOCK_STREAM, 0);   // creazione socket TCP lato client

    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));   // permette l'immediato riutilizzo dell'indirizzo dopo la terminazione del processo

    memset(&addr, 0, len);   // pulizia memoria
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    
    if (type == 1) {
        ret = connect(sd, (struct sockaddr*)&addr, len);

        if (ret < 0) {
            perror("Errore in fase di connessione");
            printf("%s", "\n");

            return -1;  // faccio gestire al chiamante l'errore
        }
    }
    else if (type == 0) {
        ret = bind(sd, (struct sockaddr*)&addr, len);

        ret = listen(sd, 10);

        if (ret < 0) {
            perror("Errore in fase di configurazione socket");
            printf("%s", "\n");

            return -1;  // faccio gestire al chiamante l'errore
        }
    }
    else {
        return -1;
    }

    return sd;
}


int main (int argc, char *argv[]) {
    int serv_port = 4242;   // per la porta inserita dall'utente (il valore iniziale è il default)

    int listener, com_sd;   // socket di ascolto e di comunicazione
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(struct sockaddr_in);

    pid_t pid;  // per salvare l'id dei processi figli creati
    struct active_process *children_list = NULL;   // lista per tenere traccia di processi attivi
    struct active_process *new_child;   // per salvare il pid del nuovo processo creato

    fd_set main_set, read_fds;  // set principale e set per controllo socket in lettura
    int max_fd;     // per tenere traccia dell'indice più alto tra i socket aperti

    int esc_required = 0;   // per verificare se è stato richiesto di chiudere il server
    

    if (argc > 2) {     // gestisco gli errori all'avvio
        printf("Numero di parametri ricevuti all'avvio non corretto");
        exit(-1);
    }
    else if (argc == 2) {
        serv_port = atoi(argv[1]);  // converto la stringa in intero
    }


    // stampo a video il messaggio iniziale del server
    system("clear");    // pulisco il terminale
    printf("%s\n%s\n%s\n%s\n%s\n",
        "***************************** SERVER STARTED *********************************\n",
        "Digita un comando:",
        "1) help --> mostra i dettagli dei comandi",
        "2) list --> mostra un elenco degli utenti connessi",
        "3) esc --> chiude il server\n"
    );


    FD_ZERO(&main_set);     // azzero i set
    FD_ZERO(&read_fds);

    mkdir("./SERVER", 0755);    // se non esiste già costruisco la cartella base per i file del server
    mkdir("./SERVER/PENDING", 0755);    // costruisco la cartella per i log dei messaggi pendenti

    listener = socket_configurator(serv_port, 0);    // creo un socket di ascolto per il server

    if (listener < 0) {
        exit(-1);
    }

    FD_SET(0, &main_set);   // inserisco lo standard input nel main set
    FD_SET(listener, &main_set);  // inserisco il socket di ascolto nel main set
    max_fd = listener;


    while (!esc_required) {
        read_fds = main_set;    // riempio il set da passare alla select
        select(max_fd + 1, &read_fds, NULL, NULL, NULL);    // attendo finché almeno uno dei buffer non risulta pronto per la lettura


        if (FD_ISSET(0, &read_fds)) {
            esc_required = cmd_handler();      // gestisco gli input da tastiera
        }

        if (FD_ISSET(listener, &read_fds)) {
            com_sd = accept(listener, (struct sockaddr*)&client_addr, &len);

            pid = fork();

            if (pid == 0) {
                close(listener);    // chiudo il socket di ascolto che non serve al figlio

                op_handler(com_sd);

                exit(0);
            }
            else if (pid > 0) {     // sono il padre
                new_child = malloc(sizeof(struct active_process));    // riservo memoria dinamica per la struttura

                if (new_child == NULL) {
                    printf("%s\n", "Impossibile allocare ulteriore memoria");

                    break;
                }

                new_child->pid = pid;
                new_child->next = children_list;    // inserisco il nuovo elemento in testa alla lista

                children_list = new_child;
            }

            close(com_sd);
        }
    }


    close(listener);
    while (children_list) {
        kill(children_list->pid, SIGTERM);

        children_list = children_list->next;
    }

    return 0;
}