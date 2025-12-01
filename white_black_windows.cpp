#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

// Enum pentru tipul firelor
typedef enum { NONE = 0, WHITE = 1, BLACK = 2 } color_t;

// Structura unui grup de fire(coada este formata din grupuri de aceeasi culoare)
typedef struct group {
    color_t color; // culoarea grupului
    int waiting;// cate fire asteapta in grup
    int to_enter; // cate fire pot intra acum
    CONDITION_VARIABLE cond; // condition variable pentru acest grup
    struct group* next;
} group_t;

// Structura principala a resursei
typedef struct {
    CRITICAL_SECTION lock;// zona critica
    group_t* head, * tail;// coada de grupuri
    color_t active_color;// culoarea activa
    int active_count;// cate fire sunt in resursa
} wb_t;

wb_t wb;

// Creare grup nou
static group_t* group_new(color_t color) {
    group_t* g = (group_t*)malloc(sizeof(group_t));
    g->color = color;
    g->waiting = 0;
    g->to_enter = 0;
    InitializeConditionVariable(&g->cond);//conditia de intrare in grup
    g->next = NULL;
    return g;
}

// Initializare
void wb_init(wb_t* w) {
    InitializeCriticalSection(&w->lock);
    w->head = w->tail = NULL;
    w->active_color = NONE;
    w->active_count = 0;
}

// Intrare în resursă
void enter_color(wb_t* w, color_t mycolor) {
    EnterCriticalSection(&w->lock);

    // 1. Daca resursa e libera sau este deja folosita de culoarea mea si primul grup nu este de culoare opusa -> intru direct
    if ((w->active_color == NONE) ||
        (w->active_color == mycolor &&
            (w->head == NULL || w->head->color == mycolor))) {
        w->active_color = mycolor;//culoarea thread-ului devine culoarea activa
        w->active_count++;//intra in resursa
        LeaveCriticalSection(&w->lock);
        return;
    }

    // 2. Altfel -> trebuie sa intru intr-un grup din coada
    group_t* g = w->tail;//luam ultimul grup din coada
    // daca nu exista grup sau grupul de la coada este de alta culoare
    if (g == NULL || g->color != mycolor) {
        // cream grup nou
        group_t* ng = group_new(mycolor);
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
        SleepConditionVariableCS(&g->cond, &w->lock, INFINITE);
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
        free(g);
    }

    LeaveCriticalSection(&w->lock);
}

// Ieșire din resursă
void leave_color(wb_t* w, color_t mycolor) {
    EnterCriticalSection(&w->lock);

    w->active_count--;//un thread a iesit

    // daca nu mai este niciun thread in resursa
    if (w->active_count == 0) {
        group_t* g = w->head;//urmatorul grup din coada
        if (g) {
            // activam tot grupul urmator
            g->to_enter = g->waiting;
            WakeAllConditionVariable(&g->cond);//trezim toate thread-urile din grup
        }
        else {
            w->active_color = NONE;//resursa devine libera
        }
    }

    LeaveCriticalSection(&w->lock);
}

// Funcția thread-ului
DWORD WINAPI thread_func(LPVOID arg) {
    color_t c = (color_t)(size_t)arg;

    enter_color(&wb, c);

    printf("Thread %lu INTRA (%s)\n", GetCurrentThreadId(),
        c == WHITE ? "WHITE" : "BLACK");

    Sleep(100); // 0.1 sec folosire resursă

    printf("Thread %lu IESE  (%s)\n", GetCurrentThreadId(),
        c == WHITE ? "WHITE" : "BLACK");

    leave_color(&wb, c);

    return 0;
}

// Curățare resurse
void wb_cleanup(wb_t* w) {
    // Blochez mutexul pentru a proteja accesul la structura
    EnterCriticalSection(&w->lock);

    // Parcurg toate grupurile din coada
    group_t* g = w->head;
    while (g != NULL) {
        group_t* next = g->next;// Salvez urmatorul grup
        free(g); // Eliberez memoria grupului curent
        g = next;// Trec la urmatorul grup
    }

    // Resetam head si tail, coada devine goala
    w->head = w->tail = NULL;

    LeaveCriticalSection(&w->lock);
    DeleteCriticalSection(&w->lock);
}

// Program principal
int main() {
    wb_init(&wb);

    const int N = 12;
    HANDLE threads[N];

    for (int i = 0; i < N; i++) {
        color_t c = (i % 2 == 0 ? WHITE : BLACK);
        threads[i] = CreateThread(NULL, 0, thread_func, (LPVOID)(size_t)c, 0, NULL);
        Sleep(30);
    }

    WaitForMultipleObjects(N, threads, TRUE, INFINITE);

    for (int i = 0; i < N; i++)
        CloseHandle(threads[i]);

    wb_cleanup(&wb);

    return 0;
}
