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
#define SHARE_FILE 7        // tipo msg: notifica che il prossimo messaggio sarà un file


// definisco delle strutture globali di utilità
struct chat_info {      // contiene le informazioni sulle chat
    char chat_name[FILENAME_SIZE];     // i nomi dei file per le chat sono del tipo "username.bin"
    int active;
    int members_num;    // serve a controllare che non si superi il limite massimo di utenti in un gruppo (10)
    fd_set members;
    int fd_max;
};

struct user_info {      // permette di mantenere le informazioni sull'utente
    char username[USER_DATA_SIZE];
    int chatting;   // indica se l'utente ha una chat corrente
    struct chat_info current_chat;
} logged_user;      // creo la variabile globale che conterrà i dati dell'utente loggato

struct chat_message {   // definisce il formato di un messaggio di chat
    char sender[USER_DATA_SIZE];
    char chat_name[USER_DATA_SIZE];     // utile per recuperare chat di gruppo
    int type;                       // indica il tipo di messaggio
    char submission_timestamp[TIME_SIZE];
    char receipt_timestamp[TIME_SIZE];
    char message[MESSAGE_SIZE];
};

int is_server_on;   // variabile globale che indica se il server è attivo o meno


// inserisco i prototipi di tutte le funzioni costruite
void hanging(int sd);

void show_pending(int sd, char *username);
void update_pending(char *chat_name, char *receipt_timestamp);

int open_chat(int sd, char *username);
int write_msg(int serv_sd);
int read_msg(int sd);
void server_notification(int serv_sd, int listener, fd_set *main_set, int *fd_max);
void store_msg(struct chat_message msg, char *chat_name);
void show_chat_log(char *chat_name);
void get_online_list(int serv_sd);
int add_member(int serv_sd, char *msg, char *group_name, int *new_member_port);
int share_file(char *file_path);
int accept_file(int sd, char *file_name);
void update_active_chat(struct chat_info new_chat);
void deactivate_chat(int user_sd, fd_set *main_set, int *fd_max, int serv_sd);
void close_chat_by_name(char *chat_name);
void close_chat_by_socket(int sd);

void add_contact(int sd, char *username);

int login_handler(int *serv_sd, int port);
int cmd_handler(int sd, fd_set *main_set, int *fd_max);

int send_msg(int sd, char *buffer);
int recv_msg(int sd, char *buffer);
int send_chat_msg(int sd, struct chat_message msg);
int recv_chat_msg(int sd, struct chat_message *msg);
int send_file(int sd, FILE *fptr, int to_be_sent);
int recv_file(int sd, FILE *fptr);

struct chat_info get_chat_info(char *username);
int is_friend(char *username);
void show_menu(int is_logged);
void set_timestamp(char *timestamp, int type);
int socket_configurator(int serv_port, int type);



// Funzione che si occupa del comando hanging, mostrando a video la lista degli utenti da cui l'utente ha
// ricevuto messaggi quando era offline
void hanging (int sd) {
    char buffer[BUFF_SIZE];
    int ret;


    if (!is_server_on) {    // se il server è offline, non vado avanti
        printf("%s\n\n", "Impossibile contattare il server: server disconnesso");
        return;
    }

    strcpy(buffer, "hanging");
    ret = send_msg(sd, buffer);       // invio la richiesta al server

    if (ret < 0) {
        close(sd);

        perror("Errore in fase di invio richiesta al server");
        printf("%s", "\n");

        return;
    }


    ret = recv_msg(sd, buffer);   // attendo la risposta del server

    if (ret < 0) {
        close(sd);

        perror("Errore in fase di ricezione risposta dal server");
        printf("%s", "\n");

        return;
    }
    else if (ret == 0) {
        close(sd);

        printf("%s\n", "Connessione con il server persa\n");
        return;
    }


    printf("%s\n", buffer);     // stampo la lista delle chat da cui ho ricevuto messaggi quando ero offline
}


// Funzione che richiede al server i messaggi pendenti ricevuti da uno specifico utente, li salva nel chat log
// e li mostra a video
void show_pending (int sd, char *username) {
    char buffer[BUFF_SIZE];

    struct chat_message msg;    // per recuperare i messaggi pendenti
    int msg_num = 0;    // indica quanti messaggi sono stati ricevuti
    int ret;


    if (strcmp(username, logged_user.username) == 0) {
        printf("%s\n", "Non può esistere una chat non se stessi\n");
        return;
    }


    if (!is_server_on) {    // se il server è offline, non vado avanti
        printf("%s\n\n", "Impossibile contattare il server: server disconnesso");
        return;
    }


    sprintf(buffer, "show %s", username);   // ricostruisco il messaggio da inviare al server
    ret = send_msg(sd, buffer);

    if (ret < 0) {
        close(sd);

        perror("Errore in fase di invio richiesta al server");
        printf("%s", "\n");

        return;
    }


    while (1) {
        ret = recv_chat_msg(sd, &msg);

        if (ret < 0) {
            close(sd);

            perror("Errore in fase di ricezione messaggi dal server");
            printf("%s", "\n");

            return;
        }
        else if (ret == 0) {
            close(sd);

            printf("%s\n", "Connessione con il server persa\n");
            return;
        }


        if (msg.type == QUIT_MSG) {     // quando ricevo il messaggio di quit, significa che ho ricevuto tutti i messaggi pendenti dell'utente 'username'
            break;
        }
        else if (msg.type == PENDING_READ) {    // se ricevo un messaggio di avvenuta lettura, aggiorno i timestamp nel mio chat log
            update_pending(msg.chat_name, msg.submission_timestamp);    // uso il timestamp di invio, poiché, a causa dei salvataggi di tali notifiche, il timestamp di ricezione è nullo

            printf("%s ha visualizzato i tuoi messaggi\n", msg.sender);
        }
        else {
            store_msg(msg, msg.chat_name);     // salvo il messaggio nel chat log

            printf("%s: %s %s%s\n", msg.sender, msg.message, "*", "*");      // stampo il messaggio a video (non controllo i timestamp poiché sono entrambi sicuramente presenti)
        }

        msg_num++;
    }


    if (msg_num == 0) {
        printf("Nessun messaggio da %s\n", username);
    }

    printf("%s", "\n");
}


// Funzione che si occupa di impostare il timestamp di ricezione a messaggi inviati ad utenti offline a seguito
// della loro presa visione
void update_pending (char *chat_name, char *receipt_timestamp) {
    FILE *fptr;
    char file_name[FILENAME_SIZE];
    struct chat_message msg;


    sprintf(file_name, "./DEVICES/%s/CHATS/%s.bin", logged_user.username, chat_name);
    fptr = fopen(file_name, "rb+");

    if (fptr != NULL) {
        fseek(fptr, -sizeof(struct chat_message), SEEK_END);    // mi sposto sull'ultimo messaggio per scorrere il file a ritroso, in modo da leggere solo i messaggi pendenti

        while (1) {
            fread(&msg, sizeof(struct chat_message), 1, fptr);

            if (strcmp(msg.receipt_timestamp, "") != 0) {   // dato che i messaggi pendenti sono i più recenti, appena trovo un messaggio ricevuto, esco dal while
                break;
            }


            strcpy(msg.receipt_timestamp, receipt_timestamp);   // aggiorno il timestamp di ricezione

            fseek(fptr, -sizeof(struct chat_message), SEEK_CUR);
            fwrite(&msg, sizeof(struct chat_message), 1, fptr);     // aggiorno il chat log


            if (ftell(fptr) == sizeof(struct chat_message)) {   // dopo aver letto il primo messaggio del file, esco dal while (ho raggiunto la cima)
                break;
            }


            fseek(fptr, -2*sizeof(struct chat_message), SEEK_CUR);  // sposto il cursore al messaggio successivo (corrispondente al precedente in ordine cronologico)
        }

        fclose(fptr);
    }
}


// Funzione per la apertura di una nuova chat.
// Ritorna il socket con cui comunicare con l'altro utente, -1 in caso di errore
// o 0 se non è rilevante il socket utilizzato (chat già attiva, chat offline...)
int open_chat (int sd, char *username) {
    char buffer[BUFF_SIZE];
    int ret;

    struct chat_info chat;  // per recuperare le informazioni sulla chat se già esistente

    uint16_t user_port;     // per salvare la porta su cui contattare l'utente, ricevuta dal server
    int new_sd;     // socket aperto per la comunicazione con l'utente username
    int is_online;      // indica se l'utente contattato è online o meno


    chat = get_chat_info(username);     // cerco prima tra le chat già esistenti

    if (strcmp(chat.chat_name, "") != 0 && chat.active) {   // se la chat cercata è già registrata e risulta attiva, la salvo come attuale e termino
        strcpy(logged_user.current_chat.chat_name, chat.chat_name);
        logged_user.current_chat.active = chat.active;    // chat attiva
        logged_user.current_chat.members_num = chat.members_num;
        logged_user.current_chat.members = chat.members;
        logged_user.current_chat.fd_max = chat.fd_max;

        logged_user.chatting = 1;

        show_chat_log(logged_user.current_chat.chat_name);

        return 0;   // ritorno 0 perché, essendo la chat già attiva, il socket che se ne occupa è già nel main set
    }
    else if (!is_server_on) {    // impedisco la apertura di nuove chat, se il server non è attivo
        printf("%s\n\n", "Impossibile contattare il server: server disconnesso");
        return 0;
    }

    
    sprintf(buffer, "chat %s", username);
    ret = send_msg(sd, buffer);     // se la chat non è attiva (o non ancora registrata), devo chiedere al server la porta su cui è collegato l'utente username

    if (ret < 0) {
        close(sd);

        perror("Errore in fase di invio richiesta al server");
        printf("%s", "\n");

        return -1;
    }

    ret = recv_msg(sd, buffer);

    if (ret < 0) {
        close(sd);

        perror("Errore in fase di ricezione risposta dal server");
        printf("%s", "\n");

        return -1;
    }
    else if (ret == 0) {
        close(sd);

        printf("%s\n", "Connessione con il server persa\n");
        return -1;
    }


    user_port = atoi(buffer);

    if (user_port > 0) {   // se la "atoi" non ha fallito, è sicuramente la porta su cui contattare l'utente
        is_online = 1;
        new_sd = socket_configurator(user_port, 1);     // creo socket di comunicazione

        if (new_sd < 0) {
            return -1;
        }
    }
    else if (strcmp(buffer, "Utente offline") == 0) {   // se l'utente è offline, comunico con il server che salverà i messaggi pendenti
        is_online = 0;
        new_sd = sd;
    }
    else {  // in caso di errore stampo il messaggio ricevuto dal server
        printf("%s\n\n", buffer);
        return -1;
    }


    strcpy(logged_user.current_chat.chat_name, username);  // aggiorno lo stato della chat attuale dell'utente
    logged_user.current_chat.active = is_online;    // la chat è considerata attiva solo se l'utente contattato è online
    logged_user.current_chat.members_num = 2;
    FD_ZERO(&logged_user.current_chat.members);     // devo svuotare il set per togliere i socket della chat precedente
    FD_SET(new_sd, &logged_user.current_chat.members);
    logged_user.current_chat.fd_max = new_sd;

    logged_user.chatting = 1;


    if (is_online) {
        update_active_chat(logged_user.current_chat);  // aggiorno il file delle chat attive, inserendo la nuova aperta
    }


    show_chat_log(logged_user.current_chat.chat_name);

    if (!is_online) {
        printf("\n%s è offline\n\n", username);
    }


    return new_sd;
}


// Funzione che si occupa della gestione della scrittura di messaggi in chat
// Legge da terminale, costruisce il messaggio secondo il formato e lo invia al destinatario della chat aperta.
// Il parametro serve solo per i messaggi speciali "\u" e "\a", che coinvolgono il server.
int write_msg (int serv_sd) {
    char raw_message[MESSAGE_SIZE];     // conterrà il messaggio ricevuto dal terminale
    struct chat_message msg;    // conterrà il messaggio dell'utente formattato

    char group_name[USER_DATA_SIZE];    // per recuperare le informazioni sul nuovo gruppo
    int new_member_port;   // per recuperare la porta dell'utente da aggiungere alla chat
    int new_sd;     // il socket costruito per la comunicazione con il nuovo client

    FILE *fptr;
    char *file_name;    // per recuperare il nome del file dalla richiesta di "share"
    char file_path[FILENAME_SIZE];    // per il percorso del file da condividere

    int i;  // per i cicli
    int ret;


    fgets(raw_message, MESSAGE_SIZE, stdin);

    if (strcmp(raw_message, "\n") == 0) {   // non permetto l'invio di messaggi vuoti
        return 0;
    }

    if (raw_message[strlen(raw_message) - 1] == '\n') {
        raw_message[strlen(raw_message) - 1] = '\0';    // elimino il ritorno di carrello dai messaggi
    }


    if (strcmp(raw_message, "\\q") == 0) {  // chiudo la chat (se di coppia questa passa in background, se offline viene chiusa, se gruppo notifico agli altri di chiudere)
        if (logged_user.current_chat.members_num == 2) {    // se la chat è di coppia elimino subito le informazioni sulla chat corrente (per il gruppo me ne occupo dopo con la disattivazione della chat)
            logged_user.chatting = 0;

            strcpy(logged_user.current_chat.chat_name, "");
            FD_ZERO(&logged_user.current_chat.members);
            logged_user.current_chat.fd_max = 0;

            show_menu(1);
        }

        msg.type = QUIT_MSG;    // imposto solo il tipo, poiché non mi interessa il contenuto degli altri campi
    }
    else if (strcmp(raw_message, "\\u") == 0) {     // mostra la lista degli utenti online
        if (!is_server_on) {    // se il server è offline impedisco l'invio della richiesta
            printf("%s\n\n", "Impossibile contattare il server: server disconnesso");
            return 0;
        }

        get_online_list(serv_sd);

        return 0;     // questo tipo di messaggio serve solo all'utente richiedente e non viene quindi inviato agli altri utenti
    }
    else if (strncmp(raw_message, "\\a", 2) == 0) {     // richiesta di aggiunta nuovo membro alla chat
        if (!is_server_on) {    // se il server è offline impedisco l'invio della richiesta
            printf("%s\n\n", "Impossibile contattare il server: server disconnesso");
            return 0;
        }


        if (logged_user.current_chat.active) {
            ret = add_member(serv_sd, raw_message, group_name, &new_member_port);

            if (ret == 1) {     // se posso aggiungere, costruisco il messaggio da inviare agli altri membri della chat
                strcpy(msg.sender, logged_user.username);
                
                if (logged_user.current_chat.members_num == 2) {    // se è una chat di coppia il nome della chat è identificato dall'utente con cui si comunica, diverso quindi fra le due parti
                    strcpy(msg.chat_name, logged_user.username);
                }
                else {  // se è una chat di gruppo invio il nome effettivo della chat
                    strcpy(msg.chat_name, logged_user.current_chat.chat_name);
                }

                msg.type = ADD_MEMBER;
                set_timestamp(msg.submission_timestamp, 1);
                set_timestamp(msg.receipt_timestamp, 1);
                sprintf(msg.message, "%s %d", group_name, new_member_port);     // inoltro le infomazioni come contenuto del messaggio
            }
            else {
                return 0;
            }
        }
        else {
            printf("%s\n", "Un gruppo richiede che tutti i membri siano online\n");
        }
    }
    else if (strncmp(raw_message, "share ", 6) == 0) {
        if (logged_user.current_chat.active) {      // secondo le specifiche la condivisione dei file non deve coinvolgere il server, quindi posso condividere file solo su chat attive
            strtok_r(raw_message, " ", &file_name);

            if (strcmp(file_name, "") == 0 || strchr(file_name, ' ')) {
                printf("%s\n", "Formato dell'operazione non corretto.\n");
                return 0;
            }

            sprintf(file_path, "./DEVICES/%s/FILES/%s", logged_user.username, file_name);   // definisco il percorso fisso dei file per poterlo aprire

            if ((fptr = fopen(file_path, "rb")) == NULL) {
                printf("%s\n", "File non presente in \"FILES\".\n");
                return 0;
            }
            else {
                fclose(fptr);
            }

            strcpy(msg.sender, logged_user.username);   // costruisco il messaggio per preparare i membri della chat alla ricezione di un file
            
            if (logged_user.current_chat.members_num == 2) {    // se è una chat di coppia il nome della chat è identificato dall'utente con cui si comunica, diverso quindi fra le due parti
                strcpy(msg.chat_name, logged_user.username);
            }
            else {  // se è una chat di gruppo invio il nome effettivo della chat
                strcpy(msg.chat_name, logged_user.current_chat.chat_name);
            }

            msg.type = SHARE_FILE;
            set_timestamp(msg.submission_timestamp, 1);
            set_timestamp(msg.receipt_timestamp, 1);
            strcpy(msg.message, file_name);    // il messaggio contiene il nome del file
        }
        else {
            printf("%s\n", "Impossibile condividere file\n");
            return 0;
        }
    }
    else {
        strcpy(msg.sender, logged_user.username);     // formatto il messaggio da inviare

        if (logged_user.current_chat.members_num == 2) {    // se è una chat di coppia il nome della chat è identificato dall'utente con cui si comunica, diverso quindi fra le due parti
            strcpy(msg.chat_name, logged_user.username);
        }
        else {  // se è una chat di gruppo invio il nome effettivo della chat
            strcpy(msg.chat_name, logged_user.current_chat.chat_name);
        }

        msg.type = CHAT_MSG;
        set_timestamp(msg.submission_timestamp, 1);
        
        if (logged_user.current_chat.active) {
            set_timestamp(msg.receipt_timestamp, 1);
        }
        else {  // se la chat non è attiva (sto comunicando con il server), i messaggi non avranno un tempo di ricezione
            set_timestamp(msg.receipt_timestamp, 0);
        }

        strcpy(msg.message, raw_message);
    }


    for (i = 0; i <= logged_user.current_chat.fd_max; i++) {    // invio il messaggio a tutti i membri della chat corrente
        if (FD_ISSET(i, &logged_user.current_chat.members)) {
            ret = send_chat_msg(i, msg);

            if (ret < 0) {
                if (errno == ECONNRESET) {      // un partecipante alla chat è andato offline
                    if (!logged_user.current_chat.active) {     // la chat era offline, quindi è il server che si è disconnesso
                        close(i);

                        show_menu(1);
                        printf("%s\n", "Connessione con il server persa");

                        return 0;   // ritorno 0 perché la chat era già disattiva
                    }
                    else {
                        return -1;
                    }
                }
                else {
                    close(i);

                    perror("Errore in fase di scrittura messaggio");
                    printf("%s", "\n");

                    exit(-1);
                }
            }
        }
    }


    if (msg.type == QUIT_MSG && logged_user.current_chat.members_num > 2) {
        return -1;   // se chiudo una chat di gruppo, procedo con la disattivazione
    }
    else if (msg.type == ADD_MEMBER) {
        new_sd = socket_configurator(new_member_port, 1);   // costruisco il nuovo socket per la comunicazione con il nuovo membro della chat

        if (new_sd < 0) {
            return -1;   // è già stato stampato il messaggio di errore dalla socket_configurator, quindi mi limito a chiudere la funzione con un codice di errore
        }

        close_chat_by_name(logged_user.current_chat.chat_name);     // prima di entrare nel gruppo, chiudo la chat precedente


        strcpy(logged_user.current_chat.chat_name, group_name); // aggiorno le informazioni sulla chat corrente
        logged_user.current_chat.active = 1;
        logged_user.current_chat.members_num++;
        FD_SET(new_sd, &logged_user.current_chat.members);  // aggiungo il nuovo socket
        logged_user.current_chat.fd_max = (new_sd > logged_user.current_chat.fd_max) ? new_sd : logged_user.current_chat.fd_max;

        logged_user.chatting = 1;


        show_chat_log(logged_user.current_chat.chat_name);

        return new_sd;
    }
    else if (msg.type == SHARE_FILE) {
        ret = share_file(file_path);

        if (ret < 0) {
            return -1;
        }

        sprintf(msg.message, "Inviato file \"%s\"", file_name);  // compongo un messaggio di default per la ricezione di file
        store_msg(msg, logged_user.current_chat.chat_name);

        printf("%s\n", msg.message);
    }
    else if (msg.type == CHAT_MSG) {
        store_msg(msg, logged_user.current_chat.chat_name);     // inserisco il nuovo messaggio nel file di log
    }


    return 0;
}


// Funzione che si occupa della gestione dei diversi messaggi ricevibili dall'utente
int read_msg (int sd) {
    struct chat_message msg;
    int ret;

    char *group_name, *new_member_port;     // per recuperare le informazioni da un messaggio di tipo ADD_MEMBER
    int new_sd;
    struct chat_info sender_chat;   // per recuperare le informazioni sulla chat del richiedente di aggiunta gruppo

    char file_name[FILENAME_SIZE];  // per il salvataggio del nome del file ricevuto


    ret = recv_chat_msg(sd, &msg);

    if (ret < 0) {
        close(sd);

        perror("Errore in fase di ricezione messaggio");
        printf("%s", "\n");

        exit(-1);
    }
    else if (ret == 0) {
        if (!logged_user.current_chat.active) {     // la chat era offline, quindi è il server che si è disconnesso
            close(sd);

            show_menu(1);
            printf("%s\n", "Connessione con il server persa");

            return 0;   // ritorno 0 perché la chat era già disattiva
        }
        else {
            return -1;       // uno dei partecipanti è andato offline
        }
    }


    if (msg.type == CHAT_MSG) {
        store_msg(msg, msg.chat_name);

        if (logged_user.chatting && strcmp(msg.chat_name, logged_user.current_chat.chat_name) == 0) {   // se è la chat corrente stampo il messaggio a video
            printf("%s: %s %s%s\n", msg.sender, msg.message, (strcmp(msg.submission_timestamp, "") != 0) ? "*" : "", (strcmp(msg.receipt_timestamp, "") != 0) ? "*" : "");
        }
        else {  // se la chat non è la corrente, stampo solo una notifica
            printf("\nNuovo messaggio da %s\n\n", msg.sender);
        }
    }
    else if (msg.type == ADD_MEMBER) {
        group_name = strtok_r(msg.message, " ", &new_member_port);  // recupero le informazioni sul nuovo gruppo dal messaggio

        new_sd = socket_configurator(atoi(new_member_port), 1);    // costruisco il nuovo socket per la comunicazione con il nuovo membro della chat

        if (new_sd < 0) {
            return -1;
        }


        strcpy(logged_user.current_chat.chat_name, group_name); // aggiorno le informazioni sulla chat corrente
        logged_user.current_chat.active = 1;
        logged_user.current_chat.members_num++;

        if (!logged_user.chatting || strcmp(logged_user.current_chat.chat_name, msg.chat_name) != 0) {  // se non sono nella chat da cui arriva la richiesta
            sender_chat = get_chat_info(msg.sender);    // recupero le informazioni sulla chat da cui arriva la richiesta

            close_chat_by_name(sender_chat.chat_name);  // la chiudo, perché userò lo stesso socket

            FD_ZERO(&logged_user.current_chat.members);     // pulisco il set corrente
            logged_user.current_chat.fd_max = 0;

            FD_SET(sender_chat.fd_max, &logged_user.current_chat.members);  // aggiungo il socket del richiedente
            FD_SET(new_sd, &logged_user.current_chat.members);  // aggiungo il nuovo socket
            logged_user.current_chat.fd_max = (new_sd > sender_chat.fd_max) ? new_sd : sender_chat.fd_max;
        }
        else {
            if ((logged_user.current_chat.members_num -1) == 2) {   // se la precedente chat era una chat privata, la chiudo
                close_chat_by_name(msg.chat_name);
            }

            FD_SET(new_sd, &logged_user.current_chat.members);  // aggiungo il nuovo socket
            logged_user.current_chat.fd_max = (new_sd > logged_user.current_chat.fd_max) ? new_sd : logged_user.current_chat.fd_max;
        }

        logged_user.chatting = 1;


        show_chat_log(logged_user.current_chat.chat_name);

        return new_sd;
    }
    else if (msg.type == SHARE_FILE) {
        ret = accept_file(sd, msg.message);     // il messaggio contiene il nome del file condiviso

        if (ret == 0) {
            close(sd);
            return -1;
        }

        strcpy(file_name, msg.message);
        sprintf(msg.message, "Inviato file \"%s\"", file_name);  // compongo un messaggio di default per la ricezione di file

        store_msg(msg, msg.chat_name);

        if (logged_user.chatting && strcmp(msg.chat_name, logged_user.current_chat.chat_name) == 0) {   // se è la chat corrente stampo il messaggio a video
            printf("%s: %s %s%s\n", msg.sender, msg.message, (strcmp(msg.submission_timestamp, "") != 0) ? "*" : "", (strcmp(msg.receipt_timestamp, "") != 0) ? "*" : "");
        }
        else {  // se la chat non è la corrente, stampo solo una notifica
            printf("\nNuovo messaggio da %s\n\n", msg.sender);
        }
    }
    else if (msg.type == QUIT_MSG) {
        if (logged_user.current_chat.members_num > 2) {     // se qualcuno esce dalla chat di gruppo, la chiudo per tutti
            return -1;
        }
    }

    return 0;
}


// Funzione che si occupa di ricevere una notifica dal server.
// Se è una notifica di avvenuta lettura di messaggi pending, aggiorno il relativo chat log, mentre se è una
// richiesta di partecipazione a chat o gruppo, recupero le informazioni necessarie alla creazione di connessioni
// con gli utenti partecipanti alla chat.
// Riceve come parametri il socket su cui riceverà il messaggio dal server, il socket di ascolto per ricevere
// le richieste di connessione dei membri della nuova chat e dei riferimenti per aggiornare il main set.
void server_notification (int serv_sd, int listener, fd_set *main_set, int *fd_max) {
    struct chat_message msg;
    int ret;

    struct chat_info chat;      // per recuperare le infomazioni sulla chat da aprire

    struct sockaddr_in client_addr;
    socklen_t addr_len;
    int new_sd;     // socket generati dalla accept delle richieste dei membri della chat
    fd_set set;
    int new_max_fd = 0;     // conterrà il massimo fd tra i nuovi generati dalla accept
    int i;      // per i cicli

    char user_answer[USER_DATA_SIZE];      // contiene la risposta alla domanda "aprire chat"


    ret = recv_chat_msg(serv_sd, &msg);

    close(serv_sd);     // il socket di comunicazione con il server non mi serve più

    if (ret <= 0) {     // per la ricezione di una notifica da server non mi interessa differenziare le casistiche di ret
        perror("Errore in fase di ricezione notifica dal server");
        printf("%s", "\n");

        return;
    }


    if (msg.type == CHAT_REQ) {
        chat = get_chat_info(msg.sender);   // se la chat esiste già la recupero

        if (strcmp(chat.chat_name, "") == 0) {  // se non esiste inserisco le informazioni ricevute dal server
            strcpy(chat.chat_name, msg.chat_name);
            chat.active = 1;
            chat.members_num = 2;
        }
        else {  // altrimenti la segno come attiva
            chat.active = 1;
        }
    }
    else if (msg.type == GROUP_REQ) {
        strcpy(chat.chat_name, msg.chat_name);
        chat.active = 1;
        chat.members_num = atoi(msg.message);   // recupero il numero di membri del gruppo dal messaggio di richiesta
    }
    else if (msg.type == PENDING_READ) {
        update_pending(msg.chat_name, msg.submission_timestamp);   // aggiorno i timestamp di ricezione nel chat log (ho bisogno di usare il timestamp di invio, poiché per la questione del salvataggio della notifica ho dovuto mettere quello di ricezione a 0)

        printf("\n%s ha ricevuto i tuoi messaggi\n", msg.sender);
        return;
    }
    else {  // tipo del messaggio non gestibile da questa funzione
        return;
    }

        
    FD_ZERO(&set);

    for (i = 1; i < chat.members_num; i++) {    // accetto una ad una le richieste dei membri della nuova chat
        new_sd = accept(listener, (struct sockaddr*)&client_addr, &addr_len);

        FD_SET(new_sd, &set);
        FD_SET(new_sd, main_set);
        new_max_fd = (new_sd > new_max_fd) ? new_sd : new_max_fd;
    }

    *fd_max = (new_max_fd > *fd_max) ? new_max_fd : *fd_max;   // aggiorno anche l'fd_max del main set

    chat.members = set;         // aggiorno le ultime informazioni sulla nuova chat
    chat.fd_max = new_max_fd;


    if (msg.type == CHAT_REQ) {
        update_active_chat(chat);   // aggiorno il file delle chat attive con le nuove informazioni ricevute

        if (strcmp(msg.chat_name, logged_user.current_chat.chat_name) != 0) {   // se non è la chat corrente
            printf("\n%s vuole chattare. Aprire la chat? [S/N]\n", msg.sender);
            scanf("%s", user_answer);   // aspetto la risposta alla richiesta di apertura chat
        }
        else {
            strcpy(user_answer, "S");   // se è la chat corrente faccio il cambio in automatico
        }
    }
    else {
        strcpy(user_answer, "S");   // se è un gruppo apro automaticamente la chat senza permettere all'utente di scegliere
    }


    if (strcmp(user_answer, "S") == 0) {    // se la risposta è positiva, la nuova chat diventa quella corrente
        if (!logged_user.current_chat.active || logged_user.current_chat.members_num > 2) {     // prima di cambiare chat, chiudo la eventuale chat offline o chat di gruppo corrente
            msg.type = QUIT_MSG;

            for (i = 0; i <= logged_user.current_chat.fd_max; i++) {
                if (FD_ISSET(i, &logged_user.current_chat.members)) {
                    ret = send_chat_msg(i, msg);

                    if (ret < 0) {
                        if (errno == ECONNRESET) {      // un partecipante alla chat è andato offline
                            return;
                        }
                        else {
                            close(i);

                            perror("Errore in fase di scrittura messaggio");
                            printf("%s", "\n");

                            exit(-1);
                        }
                    }
                }
            }
        }

        if (strcmp(msg.chat_name, logged_user.current_chat.chat_name) != 0) {
            show_chat_log(chat.chat_name);      // mostro la cronologia della nuova chat
        }
        else {  // se è la chat corrente, il chat log è già visibile e quindi mi limito a notificare che l'utente è online
            printf("\n%s è online\n\n", msg.sender);
        }

        strcpy(logged_user.current_chat.chat_name, chat.chat_name);
        logged_user.current_chat.active = chat.active;
        logged_user.current_chat.members_num = chat.members_num;
        logged_user.current_chat.members = chat.members;
        logged_user.current_chat.fd_max = chat.fd_max;

        logged_user.chatting = 1;
    }
    else {
        printf("%s", "\n");
    }
}


// Funzione che si occupa di memorizzare un messaggio nel file di log della chat su cui è stato inviato
void store_msg (struct chat_message msg, char *chat_name) {
    FILE *fptr;
    char file_name[FILENAME_SIZE];
    

    sprintf(file_name, "./DEVICES/%s/CHATS/%s.bin", logged_user.username, chat_name);
    fptr = fopen(file_name, "ab");  // apro in append in modo da salvare i nuovi messaggi in fondo al file

    fwrite(&msg, sizeof(struct chat_message), 1, fptr);

    fclose(fptr);
}


// Funzione che mostra a video la cronologia della chat
void show_chat_log (char *chat_name) {
    FILE *fptr;
    char file_name[FILENAME_SIZE];
    struct chat_message msg;    // per recuperare i vari messaggi


    system("clear");
    printf("***************************** %s *********************************\n\n", chat_name);

    sprintf(file_name, "./DEVICES/%s/CHATS/%s.bin", logged_user.username, chat_name);
    fptr = fopen(file_name, "rb");

    if (fptr != NULL) {
        while (1) {     // stampo tutti i messaggi della chat
            fread(&msg, sizeof(struct chat_message), 1, fptr);

            if (feof(fptr)) break;

            printf("%s: %s %s%s\n",
                (strcmp(msg.sender, logged_user.username) == 0) ? "Tu" : msg.sender,
                msg.message,
                "*",
                (strcmp(msg.receipt_timestamp, "") != 0) ? "*" : msg.receipt_timestamp
            );
        }

        fclose(fptr);
    }
}


// Funzione per la gestione del messaggio speciale "\u", che si occupa di recuperare la lista degli utenti online
// e di mostrare a video quelli presenti nella rubrica dell'utente richiedente
void get_online_list (int serv_sd) {
    struct chat_message serv_msg;
    char serv_buffer[BUFF_SIZE];
    char *online_list;      // conterrà la risposta del server
    int ret;

    char *username;   // per gestire singolarmente i vari username della lista
    int users_number = 0;   // per contare quanti utenti sono online


    if (logged_user.current_chat.active) {  // differenzio tra chat attiva e non, dato che uso formati di messaggi diversi.
        strcpy(serv_buffer, "list");     // comando per la richiesta della lista degli utenti online
        ret = send_msg(serv_sd, serv_buffer);

        if (ret < 0) {
            close(serv_sd);

            perror("Errore in fase di invio richiesta al server");
            printf("%s", "\n");

            return;
        }
    }
    else {
        serv_msg.type = ONLINE_LIST_REQ;    // se ho una chat offline il server attende un messaggio in formato chat_message

        ret = send_chat_msg(serv_sd, serv_msg);

        if (ret < 0) {
            close(serv_sd);

            perror("Errore in fase di invio richiesta al server");
            printf("%s", "\n");

            return;
        }
    }


    online_list = malloc(BUFF_SIZE);

    if (online_list == NULL) {
        printf("%s\n", "Impossibile allocare ulteriore memoria");
        return;
    }

    ret = recv_msg(serv_sd, online_list);     // attendo di ricevere la lista, nel formato di stringa contenente tutti gli username degli utenti online

    if (ret < 0) {
        close(serv_sd);

        perror("Errore in fase di ricezione risposta dal server");
        printf("%s", "\n");

        return;
    }
    else if (ret == 0) {
        close(serv_sd);

        printf("%s\n", "Connessione con il server persa\n");
        return;
    }


    if (strcmp(online_list, "Nessun utente connesso al momento") == 0) {
        printf("%s\n", online_list);
    }
    else {
        printf("%s\n", "Utenti connessi:");

        while ((username = strtok_r(online_list, " ", &online_list))) {   // recupero dalla lista un utente alla volta
            if (is_friend(username)) {   // mostro a video solo se il contatto è in rubrica
                users_number++;

                printf("%d. %s\n", users_number, username);
            }
        }

        if (users_number == 0) {    // nessuno tra gli utenti online è registrato nella mia rubrica
            printf("%s\n", "Nessun utente connesso al momento");
        }
    }

    printf("%s", "\n");
}


// Funzione per l'invio della richiesta di aggiunta di un nuovo membro a una chat.
// Ritorna 1 se va a buon fine, in modo che il chiamante possa proseguire con la costruzione effettiva del gruppo.
// In quel caso gli ultimi due parametri sono utilizzati per restituire al chiamante le informazioni necessarie.
int add_member (int serv_sd, char *msg, char *group_name, int *new_member_port) {
    char buffer[BUFF_SIZE];
    int ret;

    char *operation, *new_member;   // per controllare che il formato dell'operazione richiesta sia corretto


    operation = strtok_r(msg, " ", &new_member);

    if (strcmp(operation, "\\a") != 0 ||
        strchr(new_member, ' ') || strcmp(new_member, "") == 0) {   // operazione non corretta o nome utente non valido
        printf("%s\n", "Formato dell'operazione non corretto.\n");
        return 0;
    }
    else if (logged_user.current_chat.members_num == MAX_GROUP_MEMBERS) {   // permetto gruppi di un massimo di 10 partecipanti
        printf("Numero massimo membri raggiunto\n\n");
        return 0;
    }
    else if (strcmp(logged_user.username, new_member) == 0) {     // non permetto di aggiungere se stessi
        printf("Fai già parte della chat\n\n");
        return 0;
    }
    else if (!is_friend(new_member)) {  // come per la chat, permetto la aggiunta solo di utenti registrati in rubrica
        printf("%s non è registrato nella rubrica\n\n", new_member);
        return 0;
    }
    else if (logged_user.current_chat.members_num == 2 &&
        strcmp(logged_user.current_chat.chat_name, new_member) == 0) {  // non permetto di aggiungere un utente membro della chat
        printf("L'utente fa già parte della chat\n\n");
        return 0;
    }


    sprintf(buffer, "add %s %s %d", new_member, logged_user.current_chat.chat_name, logged_user.current_chat.members_num);
    ret = send_msg(serv_sd, buffer);    // mando la richiesta al server con tutte le informazioni necessarie

    if (ret < 0) {
        close(serv_sd);

        perror("Errore in fase di invio richiesta al server");
        printf("%s", "\n");

        return -1;
    }

    ret = recv_msg(serv_sd, buffer);    // attendo dal server il nome del gruppo e la porta su cui contattare il nuovo membro

    if (ret < 0) {
        close(serv_sd);

        perror("Errore in fase di ricezione messaggi dal server");
        printf("%s", "\n");

        return -1;
    }
    else if (ret == 0) {
        close(serv_sd);

        printf("%s\n", "Connessione con il server persa\n");
        return -1;
    }


    if (strcmp(buffer, "Utente inesistente") == 0 ||
        strcmp(buffer, "Utente offline") == 0 ||
        strcmp(buffer, "L'utente fa già parte della chat") == 0 ||
        strcmp(buffer, "Impossibile completare l'operazione") == 0) {     // catturo tutti i possibili messaggi di errore
        
        printf("%s\n\n", buffer);
        return 0;
    }
    else {
        strcpy(msg, buffer);    // per la strotok ho bisogno di un puntatore, dato che un array non è accettato

        strcpy(group_name, strtok_r(msg, " ", &msg));     // recupero le informazioni da restituire al chiamante
        *new_member_port = atoi(msg);
        return 1;
    }
}


// Funzione che si occupa della condivisione di file via socket (lato mittente)
// Riceve direttamente il path del file da aprire
int share_file (char *file_path) {
    FILE *fptr;
    int file_dim;   // per salvare la dimensione del file
    int i, ret;     // per cicli e controllo valori di ritorno


    fptr = fopen(file_path, "rb");      // ho già controllato nella "write_msg" che il file esista

    fseek(fptr, 0, SEEK_END);
    file_dim = ftell(fptr);     // determino la dimensione del file

    fseek(fptr, 0, SEEK_SET);   // torno all'inizio del file per iniziare la lettura

    for (i = 0; i <= logged_user.current_chat.fd_max; i++) {    // invio il messaggio a tutti i membri della chat corrente
        if (FD_ISSET(i, &logged_user.current_chat.members)) {
            ret = send_file(i, fptr, file_dim);

            if (ret < 0) {
                fclose(fptr);

                if (errno == ECONNRESET) {      // un partecipante alla chat è andato offline
                    return -1;
                }
                else {
                    close(i);

                    perror("Errore in fase di condivisione file");
                    printf("%s", "\n");

                    exit(-1);
                }
            }
        }
    }

    fclose(fptr);

    return 1;
}


// Funzione che si occupa della condivisione di file via socket (lato destinatario)
// Riceve il nome del file che deve essere controllato nel file-system dell'utente
int accept_file (int sd, char *file_name) {
    FILE *fptr;

    char file_path[FILENAME_SIZE];      // per poter aprire il file
    char extension[FILENAME_SIZE];      // per un'eventuale ricostruzione del file_path
    char *rest;

    int ret;


    sprintf(file_path, "./DEVICES/%s/FILES/%s", logged_user.username, file_name);   // costruisco il path del file sul file system dell'utente

    fptr = fopen(file_path, "rb");

    if (fptr != NULL) {    // se il file esiste già nel mio file system, modifico il nome
        fclose(fptr);

        file_name = strtok_r(file_name, ".", &rest);     // separo il nome dall'estensione per poter aggiungere il timestamp al nome
        strcpy(extension, rest);

        sprintf(file_name, "%s%lo.%s", file_name, (long)time(NULL), extension);     // ricostruisco il nome del file aggiungendo il timestamp in secondi per renderlo univoco
        sprintf(file_path, "./DEVICES/%s/FILES/%s", logged_user.username, file_name);
    }


    fptr = fopen(file_path, "ab");  // creo un nuovo file aprendolo in append

    ret = recv_file(sd, fptr);

    fclose(fptr);

    if (ret < 0) {
        close(sd);

        perror("Errore in fase di ricezione file");
        printf("%s", "\n");

        exit(-1);
    }

    return ret;
}


// Funzione per l'aggiornamento dello stato di una vecchia chat o l'aggiunta di una nuova.
// Se esiste già, aggiorno lo stato e i socket dei membri, altrimenti inserisco in fondo.
void update_active_chat (struct chat_info new_chat) {
    FILE *fptr;
    char file_name[FILENAME_SIZE];
    struct chat_info chat;  // per recuperare i vari record dal file


    sprintf(file_name, "./DEVICES/%s/chat_register.bin", logged_user.username);
    fptr = fopen(file_name, "rb+");

    if (fptr != NULL) {
        while (1) {
            fread(&chat, sizeof(struct chat_info), 1, fptr);

            if (feof(fptr)) break;

            if (strcmp(chat.chat_name, new_chat.chat_name) == 0) {  // la chat indicata esiste già
                fseek(fptr, -sizeof(struct chat_info), SEEK_CUR);   // mi posiziono all'altezza del record da modificare

                break;
            }
        }
    }
    else {  // se il file non esiste, lo creo aprendolo in append
        fptr = fopen(file_name, "ab");
    }

    fwrite(&new_chat, sizeof(struct chat_info), 1, fptr);   // inserisco la nuova chat in fondo al file

    fclose(fptr);
}


// Funzione che si occupa di disattivare una chat del caso in cui uno dei suoi partecipanti vada offline,
// iniziando la comunicazione con il server in caso di chat di coppia corrente o chiudendola in caso di gruppo
void deactivate_chat (int user_sd, fd_set *main_set, int *fd_max, int serv_sd) {
    char serv_msg[BUFF_SIZE];

    int i;  // per i cicli
    int ret;

    
    if (FD_ISSET(user_sd, &logged_user.current_chat.members)) {     // la chat da disattivare è la corrente
        logged_user.current_chat.active = 0;    // disattivo la chat corrente

        if (logged_user.current_chat.members_num == 2) {    // se è una chat di due utenti continuo la conversazione inviando messaggi pendenti al server
            FD_CLR(user_sd, main_set);  // rimuovo il socket dal main_set in modo da non controllarlo più
            close(user_sd);

            FD_ZERO(&logged_user.current_chat.members);

            FD_SET(serv_sd, &logged_user.current_chat.members);     // rendo il server il destinatario della chat
            logged_user.current_chat.fd_max = serv_sd;

            close_chat_by_name(logged_user.current_chat.chat_name);     // aggiorno lo stato della chat anche sul file "chat_register"

            printf("\n%s si è disconnesso\n\n", logged_user.current_chat.chat_name);


            if (!is_server_on) {    // se il server non è attivo, evito la richiesta di apertura chat offline
                logged_user.chatting = 0;

                strcpy(logged_user.current_chat.chat_name, "");
                FD_ZERO(&logged_user.current_chat.members);
                logged_user.current_chat.fd_max = 0;
                
                show_menu(1);
                printf("%s\n\n", "Impossibile aprire la chat offline: server disconnesso");

                return;
            }

            sprintf(serv_msg, "chat %s", logged_user.current_chat.chat_name);
            ret = send_msg(serv_sd, serv_msg);    // mando una richiesta di chat al server per poter avviare la chat offline

            if (ret <= 0) {
                close(serv_sd);

                show_menu(1);
                perror("Impossibile aprire la chat offline");
                printf("%s", "\n");

                return;
            }

            ret = recv_msg(serv_sd, serv_msg);

            if (ret <= 0) {
                close(serv_sd);

                show_menu(1);
                perror("Impossibile aprire la chat offline");
                printf("%s", "\n");

                return;
            }
        }
        else {  // se la chat corrente è un gruppo
            usleep(10000);     // devo attendere che tutti i membri abbiano ricevuto il messaggio di quit

            for (i = 0; i <= logged_user.current_chat.fd_max; i++) {
                if (FD_ISSET(i, &logged_user.current_chat.members)) {       // elimino dal main_set e chiudo

                    FD_CLR(i, main_set);
                    close(i);
                }
            }

            strcpy(logged_user.current_chat.chat_name, "");     // elimino le informazioni sulla chat corrente
            FD_ZERO(&logged_user.current_chat.members);
            logged_user.current_chat.fd_max = 0;

            logged_user.chatting = 0;
            show_menu(1);

            printf("%s\n\n", "La chat di gruppo è stata chiusa");
        }
    }
    else {  // la chat da disattivare è una chat in background
        FD_CLR(user_sd, main_set);  // rimuovo il socket dal main_set in modo da non controllarlo più
        close(user_sd);

        close_chat_by_socket(user_sd);
    }
}


// Funzione che chiude la chat indicata.
// Se il parametro ricevuto è una stringa vuota, chiude tutte le chat attive.
void close_chat_by_name (char *chat_name) {
    FILE *fptr;
    char file_name[FILENAME_SIZE];
    struct chat_info chat;


    sprintf(file_name, "./DEVICES/%s/chat_register.bin", logged_user.username);
    fptr = fopen(file_name, "rb+");

    if (fptr != NULL) {
        while (1) {
            fread(&chat, sizeof(struct chat_info), 1, fptr);

            if (feof(fptr)) break;

            if (chat.active) {
                if (strcmp(chat_name, "") == 0) {   // richiesta di eliminare tutte le chat attive
                    chat.active = 0;

                    fseek(fptr, -sizeof(struct chat_info), SEEK_CUR);
                    fwrite(&chat, sizeof(struct chat_info), 1, fptr);
                }
                else if (strcmp(chat.chat_name, chat_name) == 0) {
                    chat.active = 0;

                    fseek(fptr, -sizeof(struct chat_info), SEEK_CUR);
                    fwrite(&chat, sizeof(struct chat_info), 1, fptr);

                    break;
                }
            }
        }

        fclose(fptr);
    }
}


// Funzione che, a partire dal socket, recupera la chat relativa e la chiude.
// Uso nomi diversi per le funzioni di close perché non esiste overloading.
void close_chat_by_socket (int sd) {
    FILE *fptr;
    char file_name[FILENAME_SIZE];
    struct chat_info chat;

    
    sprintf(file_name, "./DEVICES/%s/chat_register.bin", logged_user.username);
    fptr = fopen(file_name, "rb+");

    while (1) {     // recupero la chat da disattivare da "chat_register" tramite socket registrato
        fread(&chat, sizeof(struct chat_info), 1, fptr);

        if (feof(fptr)) break;

        if (chat.active && FD_ISSET(sd, &chat.members)) {
            chat.active = 0;

            fseek(fptr, -sizeof(struct chat_info), SEEK_CUR);
            fwrite(&chat, sizeof(struct chat_info), 1, fptr);   // aggiorno lo stato della chat sul file "chat_register"

            break;
        }
    }

    fclose(fptr);
}


// Funzione per la aggiunta di un utente alla rubrica.
// La rubrica contiene la lista degli utenti che l'utente loggato può contattare.
void add_contact (int sd, char *username) {
    char buffer[BUFF_SIZE];     // per contenere la richiesta da inviare al server
    int ret;

    FILE *fptr;
    char file_name[FILENAME_SIZE];    // per contenere il nome del file da gestire


    if (strcmp(username, logged_user.username) == 0) {
        printf("%s\n", "Impossibile aggiungere se stessi alla rubrica\n");
        return;
    }
    else if (is_friend(username)) {  // controllo di non avere l'utente già salvato in rubrica
        printf("%s\n", "Contatto già registrato in rubrica\n");
        return;
    }


    if (!is_server_on) {    // se il server è offline, non vado avanti
        printf("%s\n\n", "Impossibile contattare il server: server disconnesso");
        return;
    }


    sprintf(buffer, "check %s", username);
    ret = send_msg(sd, buffer);     // invio la richiesta al server per controllare che l'utente esista

    if (ret < 0) {
        close(sd);

        perror("Errore in fase di invio richiesta al server");
        printf("%s", "\n");

        return;
    }

    ret = recv_msg(sd, buffer);

    if (ret < 0) {
        close(sd);

        perror("Errore in fase di ricezione risposta dal server");
        printf("%s", "\n");

        return;
    }
    else if (ret == 0) {
        close(sd);

        printf("%s\n", "Connessione con il server persa\n");
        return;
    }


    if (strcmp(buffer, "Utente esistente") == 0) {
        sprintf(file_name, "./DEVICES/%s/contacts.txt", logged_user.username);
        fptr = fopen(file_name, "a");

        fseek(fptr, 0, SEEK_END);
        fprintf(fptr, "%s\n", username);     // salvo il nuovo contatto in rubrica

        printf("%s aggiunto alla rubrica\n\n", username);
        fclose(fptr);
    }
    else {
        printf("%s\n\n", buffer);   // stampo il messaggio di errore
    }
}


// Funzione per la gestione di comandi ricevuti dall'utente prima di essere loggato.
// Richiede un puntatore a intero dove salvare l'identificatore del socket creato per comunicare con il server
// e la porta su cui gli altri utenti lo potranno contattare da notificare al server.
// Restituisce un intero che indica se l'utente è stato loggato con successo
int login_handler (int *serv_sd, int port) {
    char buffer[BUFF_SIZE];
    int ret;    // per controllare i valori di ritorno delle funzioni chiamate

    char *msg_rest;     // per mantenere il resto del messaggio a seguito di una strtok
    char *operation, *param;    // per separare l'operazione richiesta dai parametri
    int serv_port;  // per recuperare la porta su cui è attivo il server
    char parameters[BUFF_SIZE];     // per memorizzare tutti i parametri ricevuti
    int param_number = 0;   // per controllare il numero di parametri ricevuti per l'operazione richiesta


    fgets(buffer, BUFF_SIZE, stdin);    // leggo un'intera riga dallo stdin e lo memorizzo in buffer


    if (strcmp(buffer, "\n") == 0) {    // risolvo il problema dell'invio semplicemente escludendo il caso dal resto delle operazioni
        return 0;
    }

    if (buffer[strlen(buffer) - 1] == '\n') {     // tolgo il '\n' dalla fine della stringa che è scomodo per i controlli
        buffer[strlen(buffer) - 1] = '\0';
    }

    operation = strtok_r(buffer, " ", &msg_rest);   // prendo la prima parola che indica l'operazione richiesta


    if (strcmp(operation, "signup") == 0 ||
        strcmp(operation, "in") == 0) {     // dato che lato client le due operazioni richiedono gli stessi parametri e ricevono la stessa risposta, le gestisco insieme
        
        while ((param = strtok_r(msg_rest, " ", &msg_rest))) {
            param_number++;     // tengo il conto del numero di parametri ricevuti dall'utente

            if (param_number == 1) {
                serv_port = atoi(param);     // il primo parametro è la porta su cui è attivo il server
                strcpy(parameters, msg_rest);   // il resto della stringa sono i parametri richiesti per l'operazione
            }
        }

        if (param_number != 3 ||
            serv_port == 0) {    // se sono stati ricevuti un numero sbagliato di parametri o se non è stato inserito correttamente il numero di porta
            printf("%s\n", "Formato dell'operazione non corretto.\n");
            return 0;
        }


        sprintf(buffer, "%s %s %d", operation, parameters, port);    // costruisco il messaggio da mandare al server


        *serv_sd = socket_configurator(serv_port, 1);  // costruisco il socket per comunicare con il server

        if (*serv_sd < 0) {
            return 0;   // è già stato stampato il messaggio di errore dalla socket_configurator, quindi mi limito a chiudere la funzione
        }


        ret = send_msg(*serv_sd, buffer);   // invio la richiesta dell'utente al server

        if (ret < 0) {
            close(*serv_sd);

            perror("Errore in fase di invio richiesta al server");
            printf("%s", "\n");

            return 0;
        }


        ret = recv_msg(*serv_sd, buffer);    // attendo dal server il messaggio con il risultato dell'operazione

        if (ret < 0) {
            close(*serv_sd);

            perror("Errore in fase di ricezione risposta dal server");
            printf("%s", "\n");

            return 0;
        }
        else if (ret == 0) {
            close(*serv_sd);

            printf("%s\n", "Connessione con il server persa\n");
            return 0;
        }

       
        if (strcmp(buffer, "Successo") == 0) {
            strcpy(logged_user.username, strtok(parameters, " "));  // aggiorno le informazioni sull'utente loggato
            logged_user.chatting = 0;

            strcpy(logged_user.current_chat.chat_name, "");     // imposto le informazioni sulla chat corrente a valori nulli
            FD_ZERO(&logged_user.current_chat.members);
            logged_user.current_chat.fd_max = 0;

            return 1;   // indico che l'operazione è andata a buon fine
        }
        else {
            close(*serv_sd);

            printf("%s\n\n", buffer);   // stampo il messaggio di errore
            return 0;
        }
    }
    else {
        printf("%s\n", "Comando non riconosciuto.\n");

        return 0;
    }
}


// Funzione per la gestione dei comandi ricevuti dall'utente loggato
// In caso di comando "chat" si occupa anche di inserire nel main set il socket che gestisce la nuova chat
int cmd_handler (int sd, fd_set *main_set, int *fd_max) {
    char cmd[BUFF_SIZE];      // per recuperare i comandi in input

    char msg_copy[BUFF_SIZE];     // per salvare una copia del messaggio su cui lavorare
    char *operation, *parameters;     // per separare l'operazione e i parametri ricevuti nel messaggio dell'utente

    int new_sd;     // per memorizzare il nuovo socket generato a seguito della apertura di una nuova chat


    fgets(cmd, BUFF_SIZE, stdin);   // memorizzo un'intera riga in cmd

    if (strcmp(cmd, "\n") == 0) {    // risolvo il problema dell'invio semplicemente escludendo il caso dal resto delle operazioni
        return 0;
    }

    if (cmd[strlen(cmd) - 1] == '\n') {     // tolgo il '\n' dalla fine della stringa che è scomodo per i controlli
        cmd[strlen(cmd) - 1] = '\0';
    }

    strcpy(msg_copy, cmd);  // salvo una copia del messaggio per poterci lavorare
    operation = strtok_r(msg_copy, " ", &parameters);   // prendo la prima parola che indica l'operazione richiesta


    if (strcmp(operation, "hanging") == 0) {    // gestione comando 'hanging'

        if (strcmp(parameters, "") != 0) {  // la hanging non deve avere parametri
            printf("%s\n", "Formato dell'operazione non corretto.\n");
            return 0;
        }

        hanging(sd);
    }
    else if (strcmp(operation, "show") == 0) {  // gestione comando 'show'

        if (strcmp(parameters, "") == 0 || strchr(parameters, ' ')) {  // la show deve avere un solo parametro (=> non devono esserci spazi nella stringa parameters, poiché ciò indicherebbe la presenza di più parametri)
            printf("%s\n", "Formato dell'operazione non corretto.\n");
            return 0;
        }

        show_pending(sd, parameters);
    }
    else if (strcmp(operation, "chat") == 0) {  // gestione comando 'chat'

        if (strcmp(parameters, "") == 0 || strchr(parameters, ' ')) {  // la chat deve avere un solo parametro (vedi "show")
            printf("%s\n", "Formato dell'operazione non corretto.\n");
            return 0;
        }

        if (strcmp(parameters, logged_user.username) == 0) {
            printf("%s\n", "Impossibile aprire una chat con se stessi\n");
            return 0;
        }

        if (is_friend(parameters)) {    // permetto di aprire una chat solo con utenti in rubrica
            new_sd = open_chat(sd, parameters);

            if (new_sd > 0 && new_sd != sd) {      // aggiorno il main_set solo se non ci sono stati errori, la chat non era già attiva e l'utente destinatario non è risultato offline
                FD_SET(new_sd, main_set);
                *fd_max = (new_sd > *fd_max) ? new_sd : *fd_max;
            }
        }
        else {
            printf("%s non è registrato nella rubrica\n\n", parameters);
        }
    }
    else if (strcmp(operation, "add") == 0) {   // gestione comando 'add'

        if (strcmp(parameters, "") == 0 || strchr(parameters, ' ')) {  // la add deve avere un solo parametro (vedi "show")
            printf("%s\n", "Formato dell'operazione non corretto.\n");
            return 0;
        }

        add_contact(sd, parameters);    // aggiungo il nuovo contatto alla rubrica
    }
    else if (strcmp(operation, "out") == 0) {   // gestione comando 'out'

        if (strcmp(parameters, "") != 0) {  // la out non deve avere parametri
            printf("%s\n", "Formato dell'operazione non corretto.\n");
        }
        else {
            printf("%s\n", "A presto");
            return 1;
        }
    }
    else {
        printf("%s\n", "Comando non riconosciuto.\n");
    }


    return 0;
}


// Funzione per l'invio di un messaggio in rete
// Il protocollo di invio richiede di notificare al destinatario la dimensione del messaggio prima di spedirlo
int send_msg (int sd, char *buffer) {
    uint16_t msg_len;
    int ret;    // per controllare i valori di ritorno delle funzioni chiamate


    msg_len = htons(strlen(buffer) + 1);    // prendo la dimensione direttamente in network order per poterla inviare

    ret = send(sd, &msg_len, sizeof(uint16_t), 0);

    if (ret < 0) return -1;     // faccio gestire le casistiche di errore al chiamante

    ret = send(sd, buffer, ntohs(msg_len), 0);

    return ret;
}


// Funzione per la ricezione di un messaggio da remoto
// Il protocollo di ricezione prevede una doppia lettura: prima la dimensione del messaggio da ricevere e poi
// il messaggio effettivo
int recv_msg (int sd, char *buffer) {
    uint16_t msg_len;
    int ret;    // per controllare i valori di ritorno delle funzioni chiamate

    
    ret = recv(sd, &msg_len, sizeof(uint16_t), 0);

    if (ret <= 0) return ret;   // faccio gestire le casistiche di errore al chiamante

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
    else msg->type = ntohl(msg->type);  // converto 4B in host order

    ret = recv(sd, msg->submission_timestamp, TIME_SIZE, 0);

    if (ret <= 0) return ret;

    ret = recv(sd, msg->receipt_timestamp, TIME_SIZE, 0);

    if (ret <= 0) return ret;

    ret = recv(sd, msg->message, MESSAGE_SIZE, 0);


    return ret;
}


// Funzione che definisce il protocollo di invio di un file:
// viene prima inviata la dimensione di questo e poi il file stesso a blocchi di dimensione fissa
int send_file (int sd, FILE *fptr, int to_be_sent) {
    char chunk[BUFF_SIZE];      // per la gestione dei singoli blocchi che compongono il file
    uint32_t dim;    // per la dimensione in network order
    int chunk_size; // indica la dimensione del blocco da trasmettere
    int ret;


    dim = htonl(to_be_sent);
    ret = send(sd, &dim, sizeof(uint32_t), 0);  // invio prima la dimensione del file

    if (ret < 0) return ret;


    while (to_be_sent > 0) {    // ciclo finché non ho inviato tutti i byte del file
        if (to_be_sent < BUFF_SIZE) {
            chunk_size = to_be_sent;
        }
        else {
            chunk_size = BUFF_SIZE;
        }


        memset(chunk, 0, chunk_size);    // pulisco la memoria

        fread(chunk, chunk_size, 1, fptr);   // recupero un blocco

        ret = send(sd, chunk, chunk_size, 0);    // invio il blocco recuperato

        if (ret < 0) return ret;

        to_be_sent -= chunk_size;    // decremento il numero di byte rimasti da inviare
    }

    return 1;
}


// Funzione che definisce il protocollo di ricezione di un file:
// si riceve prima la dimensione di esso e si cicla ricostruendo il file tramite blocchi di dimensione fissa
int recv_file (int sd, FILE *fptr) {
    char chunk[BUFF_SIZE];  // per recuperare i blocchi del file
    int to_be_received;
    int chunk_size; // indica la dimensione del blocco da ricevere
    int ret;


    ret = recv(sd, &to_be_received, sizeof(int), 0);    // recupero per prima la dimensione del file

    if (ret <= 0) return ret;

    to_be_received = ntohl(to_be_received);     // converto in host order


    while (to_be_received > 0) {
        if (to_be_received < BUFF_SIZE) {   // gestione invio ultimo blocco
            chunk_size = to_be_received;
        }
        else {
            chunk_size = BUFF_SIZE;
        }


        memset(chunk, 0, chunk_size);    // pulisco la memoria

        ret = recv(sd, chunk, chunk_size, 0);    // recupero un blocco del file

        if (ret <= 0) return ret;

        fwrite(&chunk, chunk_size, 1, fptr);     // inserisco in append il nuovo blocco nel file

        to_be_received -= chunk_size;
    }

    return 1;
}


// Funzione che permette di recuperare le informazioni di una chat dal registro delle chat
struct chat_info get_chat_info (char *username) {
    FILE *fptr;
    struct chat_info chat;
    char file_name[FILENAME_SIZE];

    int exists = 0;     // per discriminare il caso in cui la chat richiesta non sia ancora stata registrata


    sprintf(file_name, "./DEVICES/%s/chat_register.bin", logged_user.username);
    fptr = fopen(file_name, "rb");

    if (fptr != NULL) {
        while (1) {
            fread(&chat, sizeof(struct chat_info), 1, fptr);

            if (feof(fptr)) break;

            if (strcmp(chat.chat_name, username) == 0) {
                exists = 1;
                break;
            }
        }


        fclose(fptr);
    }


    if (!exists) {      // se la chat non è ancora registrata, uso dei valori di default per indicarlo al chiamante
        strcpy(chat.chat_name, "");
        chat.active = 0;    // metto anche questo a 0, poiché un valore casuale potrebbe essere fastidioso per i controlli
    }


    return chat;
}


// Funzione che indica se l'utente specificato è salvato o meno in rubrica
int is_friend (char *username) {
    FILE *fptr;
    char file_name[FILENAME_SIZE];    // per contenere il nome del file da gestire
    char contact[USER_DATA_SIZE];  // per leggere i vari contatti della rubrica


    sprintf(file_name, "./DEVICES/%s/contacts.txt", logged_user.username);
    fptr = fopen(file_name, "r");

    if (fptr != NULL) {
        while (1) {
            fscanf(fptr, "%s", contact);

            if (feof(fptr)) break;

            if (strcmp(contact, username) == 0) {   // username salvato in rubrica
                fclose(fptr);
                return 1;
            }
        }

        fclose(fptr);
    }

    return 0;
}


// Funzione che si occupa di mostrare a schermo la lista dei comandi disponibili.
// Il parametro permette di differenziare il menù prima del login e quello dopo.
void show_menu (int is_logged) {
    system("clear");

    if (!is_logged) {   // utente non loggato
        printf("%s\n%s\n%s\n%s\n",
            "***************************** DEVICE STARTED *********************************\n",
            "Digita un comando:",
            "1) signup srv_port username password --> permette di creare un nuovo account",
            "2) in srv_port username password --> permette di effettuare il login al servizio\n"
        );
    }
    else {  // utente loggato
        printf("%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
            "************************** SUCCESSFULLY LOGGED IN ******************************\n",
            "Menù dei comandi:\n",
            "1) hanging --> Mostra la lista degli utenti che hanno inviato messaggi mentre eri offline.\n",
            "2) show username --> Mostra i messaggi pendenti ricevuti, mentre eri offline, dall’utente 'username'.\n",
            "3) chat username --> Avvia una chat con l’utente username.\n",
            "4) add username --> Aggiunge l'utente 'username' alla rubrica.\n",
            "5) out --> Permette di scollegarsi dal servizio.\n"
        );
    }
}


// Funzione che restituisce una stringa contenente un timestamp nel formato "dd/MM/yyyy hh/mm"
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
    int port;   // per memorizzare la porta su cui costruire il socket di ascolto per comunicare con gli altri utenti
    int serv_sd, listener;      // socket per comunicare con il server e socket di ascolto di richieste da altri device
    char file_name[FILENAME_SIZE];    // aiuta alla gestione dei file

    fd_set main_set, read_fds;  // set principale e set per controllo socket in lettura
    int max_fd;     // per tenere traccia dell'indice più alto tra i socket aperti

    int logged = 0;     // indica se l'utente è loggato o meno
    int out_required = 0;   // per verificare se è stato richiesto di chiudere l'applicazione

    struct sockaddr_in dev_addr;
    socklen_t addr_len;
    int new_sd;     // socket creato a seguito di accettazione di richiesta di chat da un utente esterno

    int ret;    // per controllare il valore di ritorno delle funzioni che si occupano delle chat
    int i;  // per i cicli


    if (argc == 2) {
        port = atoi(argv[1]);  // converto la stringa in intero
    }
    else {      // gestisco gli errori all'avvio
        printf("%s\n", "Numero di parametri ricevuti all'avvio non corretto");
        exit(-1);
    }


    show_menu(logged);  // stampo a video il menù iniziale


    FD_ZERO(&main_set);     // azzero i set
    FD_ZERO(&read_fds);


    while (!logged) {
        logged = login_handler(&serv_sd, port);   // gestisco la ricezione di comandi dell'utente non ancora loggato
    }


    mkdir("./DEVICES", 0755);
    sprintf(file_name, "./DEVICES/%s", logged_user.username);
    mkdir(file_name, 0755);     // creo la cartella personale dell'utente loggato se non esiste già
    sprintf(file_name, "%s/CHATS", file_name);
    mkdir(file_name, 0755);     // creo una cartella per contenere tutte le chat dell'utente
    sprintf(file_name, "./DEVICES/%s/FILES", logged_user.username);
    mkdir(file_name, 0755);     // creo una cartella per i file da poter scambiare con altri utenti

    close_chat_by_name("");       // disattivo le chat che sono rimaste segnate come attive dalla scorsa sessione (all'avvio perché l'utente potrebbe aver chiuso con 'ctrl+C')

    listener = socket_configurator(port, 0);    // creo un socket di ascolto per le richieste di chat da parte di altri utenti

    if (listener < 0) {
        exit(-1);
    }

    is_server_on = 1;   // indico che il server è attivo

    FD_SET(0, &main_set);   // inserisco lo standard input nel main set
    FD_SET(serv_sd, &main_set);     // inserisco il socket di comunicazione con il server nel main set, in modo da controllarlo nel caso il server si dovesse disconnettere
    FD_SET(listener, &main_set);  // inserisco il socket di ascolto nel main set
    max_fd = (listener > serv_sd) ? listener : serv_sd;

    show_menu(logged);      // una volta effettuato il login, mostro a schermo il menù delle operazioni


    while (!out_required) {
        read_fds = main_set;    // riempio il set da passare alla select
        select(max_fd + 1, &read_fds, NULL, NULL, NULL);    // attendo finché almeno uno dei buffer non risulta pronto per la lettura


        for (i = 0; i <= max_fd; i++) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == 0) {   // gestisco la ricezione dei comandi dell'utente
                    if (!logged_user.chatting) {    // se l'utente si trova sulla schermata di menù
                        out_required = cmd_handler(serv_sd, &main_set, &max_fd);
                    }
                    else {
                        ret = write_msg(serv_sd);

                        if (ret < 0) {
                            deactivate_chat(logged_user.current_chat.fd_max, &main_set, &max_fd, serv_sd);
                        }
                        else if (ret > 0) {     // se ret > 0 è il sd del nuovo membro della chat che deve essere controllato in lettura
                            FD_SET(ret, &main_set);
                            max_fd = (ret > max_fd) ? ret : max_fd;
                        }
                    }
                }
                else if (i == listener) {   // ricevuta richiesta di connessione dal server
                    new_sd = accept(listener, (struct sockaddr*)&dev_addr, &addr_len);

                    server_notification(new_sd, listener, &main_set, &max_fd);   // attendo un messaggio di notifica da parte del server
                }
                else if (i == serv_sd) {    // ricevo un messaggio dal server in questo modo unicamente se questo si è disconnesso, quindi procedo direttamente con la gestione della situazione
                    close(serv_sd);
                    FD_CLR(serv_sd, &main_set);

                    is_server_on = 0;   // indico che il server non è più attivo


                    if (logged_user.chatting && !logged_user.current_chat.active) {     // se l'utente ha aperta una chat offline al momento della disconnessione del server, lo rimando al menù
                        logged_user.chatting = 0;

                        strcpy(logged_user.current_chat.chat_name, "");
                        FD_ZERO(&logged_user.current_chat.members);
                        logged_user.current_chat.fd_max = 0;

                        show_menu(1);
                        printf("%s\n\n", "La chat offline è stata chiusa: server disconnesso");
                    }
                }
                else {
                    ret = read_msg(i);

                    if (ret < 0) {
                        deactivate_chat(i, &main_set, &max_fd, serv_sd);
                    }
                    else if (ret > 0) {     // se ret > 0 è il sd del nuovo membro della chat che deve essere controllato in lettura
                        FD_SET(ret, &main_set);
                        max_fd = (ret > max_fd) ? ret : max_fd;
                    }
                }
            }
        }
    }


    close(serv_sd);
    close(listener);

    return 0;
}