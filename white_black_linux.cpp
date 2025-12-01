
// Initializare structura principala
//  Problema firelor alb / negru – implementare completă Linux
//  Concurență cu pthreads + coadă de grupuri (fără starvation)

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>


// Enum pentru tipul firelor
typedef enum { NONE = 0, WHITE = 1, BLACK = 2 } color_t;


// Structura unui grup de fire(coada este formata din grupuri de aceeasi culoare)
typedef struct group {
    color_t color;         // culoarea grupului
    int waiting;           // cate fire asteapta in grup
    int to_enter;          // cate fire pot intra acum
    pthread_cond_t cond;   // condition variable pentru acest grup
    struct group *next;
} group_t;


// Structura principala a resursei
typedef struct {
    pthread_mutex_t lock;  // mutex global
    group_t *head, *tail;  // coada de grupuri
    color_t active_color;  // culoarea activa
    int active_count;      // cate fire sunt in resursa
} wb_t;

wb_t wb; // resursa globala


// Functie pentru creare grup nou
static group_t* group_new(color_t color) {
    group_t *g = (group_t*)malloc(sizeof(group_t));
    g->color = color;
    g->waiting = 0;// cate fire asteapta in grup
    g->to_enter = 0;// cate fire pot intra acum
    pthread_cond_init(&g->cond, NULL);//conditia de intrare in grup
    g->next = NULL;
    return g;
}

// Initializare structura principala
void wb_init(wb_t *w) {
    pthread_mutex_init(&w->lock, NULL);
    w->head = w->tail = NULL;
    w->active_color = NONE;
    w->active_count = 0;
}


// Functie: un thread cere acces la resursa
void enter_color(wb_t *w, color_t mycolor) {
    pthread_mutex_lock(&w->lock);//cream mutex-ul care protejeaza tot sistemul

  // 1. Daca resursa e libera sau este deja folosita de culoarea mea si primul grup nu este de culoare //opusa -> intru direct
    if ( (w->active_color == NONE) ||
         (w->active_color == mycolor && 
          (w->head == NULL || w->head->color == mycolor)) ) {

        w->active_color = mycolor;//culoarea thread-ului devine culoarea activa
        w->active_count++;//intra in resursa
        pthread_mutex_unlock(&w->lock);
        return;
    }

 // 2. Altfel -> trebuie sa intru intr-un grup din coada
    group_t *g = w->tail;//luam ultimul grup din coada

    // daca nu exista grup sau grupul de la coada este de alta culoare
    if (g == NULL || g->color != mycolor) {
        // cream grup nou
        group_t *ng = group_new(mycolor);

        if (w->tail)
            w->tail->next = ng;//legam grupul nou la coada
        w->tail = ng;

	//Daca coada este goala, noul grup devine head
        if (w->head == NULL)
            w->head = ng;

        g = ng;
    }

    // se incrementeaza nr de thread-uri care asteapta in grup
    g->waiting++;

    // astept pana cand grupul meu devine activ
    while (g->to_enter == 0) {
        pthread_cond_wait(&g->cond, &w->lock);
    }

    // am fost trezit -> intru
    g->waiting--;
    g->to_enter--;
    w->active_color = mycolor;
    w->active_count++;

    // daca grupul a ramas gol -> il eliminam din coada
    if (g == w->head && g->waiting == 0 && g->to_enter == 0) {
        w->head = g->next;
        if (w->head == NULL)
            w->tail = NULL;
        pthread_cond_destroy(&g->cond);
        free(g);
    }

    pthread_mutex_unlock(&w->lock);
}


// Functie: un thread elibereaza resursa
void leave_color(wb_t *w, color_t mycolor) {
    pthread_mutex_lock(&w->lock);

    w->active_count--;//un thread a iesit

    // daca nu mai este niciun thread in resursa
    if (w->active_count == 0) {

        group_t *g = w->head;//urmatorul grup din coada

        if (g) {
            // activam TOT grupul urmator
            g->to_enter = g->waiting;
            pthread_cond_broadcast(&g->cond);//trezim toate thread-urile din grup
        } else {
            // nimeni nu asteapta
            w->active_color = NONE;//resursa devine libera
        }
    }

    pthread_mutex_unlock(&w->lock);
}


// Functia pe care o executa firele
void* thread_func(void* arg) {
    color_t c = (color_t)(long)arg;

    enter_color(&wb, c);

    printf("Thread %lu INTRA (%s)\n", pthread_self(),
           c == WHITE ? "WHITE" : "BLACK");

    usleep(100000); // foloseste resursa (0.1 sec)

    printf("Thread %lu IESE  (%s)\n", pthread_self(),
           c == WHITE ? "WHITE" : "BLACK");

    leave_color(&wb, c);

    return NULL;
}

void wb_cleanup(wb_t *w) {
    // Blochez mutexul pentru a proteja accesul la structura
    pthread_mutex_lock(&w->lock);

    // Parcurg toate grupurile din coada
    group_t *g = w->head;
    while (g != NULL) {
        group_t *next = g->next;       // Salvez urmatorul grup
        pthread_cond_destroy(&g->cond);// Distrug variabila de conditie a grupului curent
        free(g);                      // Eliberez memoria grupului curent
        g = next;                     // Trec la urmatorul grup
    }

    // Resetam head si tail, coada devine goala
    w->head = w->tail = NULL;

    // Deblochez mutexul
    pthread_mutex_unlock(&w->lock);

    // Distrug mutexul, nu mai este nevoie de el
    pthread_mutex_destroy(&w->lock);
}


// Program principal pentru test
int main() {
    wb_init(&wb);

    pthread_t th[12];

    // cream threaduri cu culori amestecate
    for (int i = 0; i < 12; i++) {
        color_t c = (i % 2 == 0 ? WHITE : BLACK);
        pthread_create(&th[i], NULL, thread_func, (void*)(long)c);
        usleep(30000); // pornire usor decalata
    }

    for (int i = 0; i < 12; i++)
        pthread_join(th[i], NULL);

 wb_cleanup(&wb);

    return 0;
}
