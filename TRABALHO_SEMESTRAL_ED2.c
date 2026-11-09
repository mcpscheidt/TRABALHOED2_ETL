/*
 * Trabalho Pratico I - Estruturas de Dados II
 * Arvore B dinamica com nos armazenados em arquivos binarios separados.
 *
 * Convencoes desta implementacao:
 * - O grau minimo t e informado pelo usuario e salvo nos metadados.
 * - Cada no tem um identificador unico. O identificador N corresponde ao
 *   arquivo no_N.bin.
 * - Os arquivos armazenam identificadores dos filhos, nao ponteiros de RAM.
 * - Os vetores em memoria usam indices de C: 0, 1, 2, ...
 * - Chaves repetidas sao rejeitadas.
 * - A insercao e a remocao seguem a estrategia top-down do CLRS apresentada
 *   nos slides especificos desses algoritmos.
 * - A impressao mostra os nos por nivel.
 *
 * Compilacao: gcc -std=c11 -Wall -Wextra -Wpedantic arvore_b_portugues.c -o arvore_b
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARQ_META "arvore_b.meta"
#define ASS_META UINT64_C(0x42545245454D4554) /* "BTREEMET" */
#define ID_NULO UINT64_C(0)
#define TAM_ENTRADA 256

typedef uint64_t IdNo;

typedef struct {
    int t;                 /* Grau minimo da arvore B. */
    IdNo raizId;        /* Identificador do no raiz; 0 significa arvore vazia. */
    IdNo proxId;        /* Proximo identificador disponivel. */
} ArvB;

typedef struct {
    IdNo id;
    int n;                 /* Quantidade atual de chaves. */
    int folha;              /* 1 se folha; 0 se no interno. */
    int *chaves;             /* Capacidade: 2*t - 1 chaves. */
    IdNo *filhos;      /* Capacidade: 2*t identificadores de filhos. */
} NoB;

static ArvB arv;

/* ---------- Utilitarios gerais e armazenamento em arquivos ---------- */

static void erroFatal(const char *msg) {
    fprintf(stderr, "Erro: %s\n", msg);
    exit(EXIT_FAILURE);
}

static int maxChaves(void) {
    return 2 * arv.t - 1;
}

static int maxFilhos(void) {
    return 2 * arv.t;
}

/* Gera o nome do arquivo correspondente a um identificador de no. */
static void nomeArqNo(IdNo id, char *buf, size_t tam) {
    int qtdEsc = snprintf(buf, tam, "no_%" PRIu64 ".bin", id);
    if (qtdEsc < 0 || (size_t)qtdEsc >= tam) {
        erroFatal("nome de arquivo de no muito longo");
    }
}

static void gravaMeta(void) {
    FILE *arq = fopen(ARQ_META, "wb");
    if (arq == NULL) {
        perror("Nao foi possivel abrir os metadados para escrita");
        exit(EXIT_FAILURE);
    }

    /* Campos escritos separadamente para nao depender do padding de struct. */
    uint64_t assin = ASS_META;
    if (fwrite(&assin, sizeof assin, 1, arq) != 1 ||
        fwrite(&arv.t, sizeof arv.t, 1, arq) != 1 ||
        fwrite(&arv.raizId, sizeof arv.raizId, 1, arq) != 1 ||
        fwrite(&arv.proxId, sizeof arv.proxId, 1, arq) != 1) {
        fclose(arq);
        erroFatal("falha ao gravar os metadados da arvore");
    }

    if (fclose(arq) != 0) {
        erroFatal("falha ao fechar o arquivo de metadados");
    }
}

static bool temMeta(void) {
    FILE *arq = fopen(ARQ_META, "rb");
    if (arq == NULL) {
        return false;
    }
    fclose(arq);
    return true;
}

static void leMeta(void) {
    FILE *arq = fopen(ARQ_META, "rb");
    if (arq == NULL) {
        erroFatal("nao foi possivel abrir os metadados existentes");
    }

    uint64_t assin = 0;
    int t = 0;
    IdNo raizId = ID_NULO;
    IdNo proxId = 0;
    bool sucesso = fread(&assin, sizeof assin, 1, arq) == 1 &&
              fread(&t, sizeof t, 1, arq) == 1 &&
              fread(&raizId, sizeof raizId, 1, arq) == 1 &&
              fread(&proxId, sizeof proxId, 1, arq) == 1;
    fclose(arq);

    if (!sucesso || assin != ASS_META || t < 2 || t > INT_MAX / 2 || proxId == 0) {
        erroFatal("arquivo de metadados invalido ou corrompido");
    }

    arv.t = t;
    arv.raizId = raizId;
    arv.proxId = proxId;
}

static NoB *alocaNo(IdNo id, int folha) {
    NoB *no = malloc(sizeof *no);
    if (no == NULL) {
        erroFatal("memoria insuficiente para alocar um no");
    }

    no->id = id;
    no->n = 0;
    no->folha = folha;
    no->chaves = calloc((size_t)maxChaves(), sizeof *no->chaves);
    no->filhos = calloc((size_t)maxFilhos(), sizeof *no->filhos);

    if (no->chaves == NULL || no->filhos == NULL) {
        free(no->chaves);
        free(no->filhos);
        free(no);
        erroFatal("memoria insuficiente para os vetores do no");
    }

    return no;
}

static void liberaNo(NoB *no) {
    if (no != NULL) {
        free(no->chaves);
        free(no->filhos);
        free(no);
    }
}

/*
 * Grava um registro de tamanho fixo para o valor atual de t.
 * Os ponteiros da memoria RAM nunca sao gravados: apenas IDs dos filhos.
 */
static void gravaNo(const NoB *no) {
    if (no == NULL || no->id == ID_NULO || no->n < 0 || no->n > maxChaves()) {
        erroFatal("tentativa de gravar um no invalido");
    }

    char caminho[64];
    nomeArqNo(no->id, caminho, sizeof caminho);
    FILE *arq = fopen(caminho, "wb");
    if (arq == NULL) {
        perror("Nao foi possivel abrir o arquivo do no para escrita");
        exit(EXIT_FAILURE);
    }

    bool sucesso = fwrite(&no->id, sizeof no->id, 1, arq) == 1 &&
              fwrite(&no->n, sizeof no->n, 1, arq) == 1 &&
              fwrite(&no->folha, sizeof no->folha, 1, arq) == 1 &&
              fwrite(no->chaves, sizeof *no->chaves, (size_t)maxChaves(), arq) == (size_t)maxChaves() &&
              fwrite(no->filhos, sizeof *no->filhos, (size_t)maxFilhos(), arq) == (size_t)maxFilhos();

    if (!sucesso) {
        fclose(arq);
        erroFatal("falha ao gravar os dados do no");
    }
    if (fclose(arq) != 0) {
        erroFatal("falha ao fechar o arquivo do no");
    }
}

/* Carrega um no do seu arquivo e reconstrui os vetores na memoria. */
static NoB *leNo(IdNo id) {
    if (id == ID_NULO) {
        erroFatal("tentativa de ler um no com identificador zero");
    }

    char caminho[64];
    nomeArqNo(id, caminho, sizeof caminho);
    FILE *arq = fopen(caminho, "rb");
    if (arq == NULL) {
        fprintf(stderr, "Nao foi possivel abrir o arquivo %s.\n", caminho);
        exit(EXIT_FAILURE);
    }

    IdNo idGravado = ID_NULO;
    int n = 0;
    int folha = 0;
    bool sucesso = fread(&idGravado, sizeof idGravado, 1, arq) == 1 &&
              fread(&n, sizeof n, 1, arq) == 1 &&
              fread(&folha, sizeof folha, 1, arq) == 1;

    if (!sucesso || idGravado != id || n < 0 || n > maxChaves() || (folha != 0 && folha != 1)) {
        fclose(arq);
        erroFatal("cabecalho de no invalido ou corrompido");
    }

    NoB *no = alocaNo(id, folha);
    no->n = n;

    sucesso = fread(no->chaves, sizeof *no->chaves, (size_t)maxChaves(), arq) == (size_t)maxChaves() &&
         fread(no->filhos, sizeof *no->filhos, (size_t)maxFilhos(), arq) == (size_t)maxFilhos();
    fclose(arq);

    if (!sucesso) {
        liberaNo(no);
        erroFatal("arquivo de no incompleto ou corrompido");
    }

    return no;
}

/* Cria um arquivo para um novo no com ID nunca utilizado nesta arvore. */
static NoB *criaNo(int folha) {
    if (arv.proxId == ID_NULO || arv.proxId == UINT64_MAX) {
        erroFatal("acabaram os identificadores disponiveis para os nos");
    }

    IdNo id = arv.proxId++;
    NoB *no = alocaNo(id, folha);
    gravaNo(no);
    gravaMeta(); /* Persiste proxId para nao reutilizar IDs ao reabrir. */
    return no;
}

static void apagaArqNo(IdNo id) {
    if (id == ID_NULO) {
        return;
    }

    char caminho[64];
    nomeArqNo(id, caminho, sizeof caminho);
    /* O arquivo pode ja ter sido removido em uma operacao anterior. */
    if (remove(caminho) != 0 && errno != ENOENT) {
        perror("Aviso: nao foi possivel remover um arquivo de no");
    }
}

/* Apaga os arquivos da arvore antiga ao escolher explicitamente uma nova. */
static void limpaArqArv(void) {
    if (!temMeta()) {
        return;
    }

    leMeta();
    for (IdNo id = 1; id < arv.proxId; id++) {
        apagaArqNo(id);
        if (id == UINT64_MAX) { /* Protecao teorica contra overflow do contador. */
            break;
        }
    }
    remove(ARQ_META);
}

/* ---------- Busca ---------- */

/* Retorna true se k existe; opcionalmente informa o ID e o indice da chave. */
static bool buscaArv(int k, IdNo *idAchado, int *posAchada) {
    IdNo idAtual = arv.raizId;

    while (idAtual != ID_NULO) {
        NoB *no = leNo(idAtual);
        int i = 0;

        /* Encontra a primeira chave que seja >= k. */
        while (i < no->n && k > no->chaves[i]) {
            i++;
        }

        if (i < no->n && no->chaves[i] == k) {
            if (idAchado != NULL) {
                *idAchado = no->id;
            }
            if (posAchada != NULL) {
                *posAchada = i;
            }
            liberaNo(no);
            return true;
        }

        /* Uma folha nao tem filhos nos quais a busca possa continuar. */
        if (no->folha) {
            liberaNo(no);
            return false;
        }

        /* O filho i contem o intervalo que pode incluir k. */
        idAtual = no->filhos[i];
        liberaNo(no);
    }

    return false;
}

/* ---------- Insercao CLRS: split, insercao em no nao cheio ---------- */

/*
 * Divide o filho cheio pai->filhos[pos].
 * A mediana sobe para o pai; as chaves menores ficam no no original e as
 * maiores passam para um novo no. Se o filho for interno, seus ponteiros
 * tambem precisam ser repartidos, sem perder nenhuma subarvore.
 */
static void divideFilho(IdNo idPai, int pos) {
    NoB *pai = leNo(idPai);
    if (pai->folha || pos < 0 || pos > pai->n) {
        liberaNo(pai);
        erroFatal("indice invalido ao dividir filho");
    }

    IdNo idFilho = pai->filhos[pos];
    NoB *y = leNo(idFilho);
    if (y->n != maxChaves()) {
        liberaNo(y);
        liberaNo(pai);
        erroFatal("split solicitado para um filho que nao esta cheio");
    }

    int t = arv.t;
    NoB *z = criaNo(y->folha);

    /* O novo no recebe as t-1 chaves maiores do filho cheio. */
    z->n = t - 1;
    for (int j = 0; j < t - 1; j++) {
        z->chaves[j] = y->chaves[j + t];
    }

    /* Para no interno, move tambem os t filhos da metade direita. */
    if (!y->folha) {
        for (int j = 0; j < t; j++) {
            z->filhos[j] = y->filhos[j + t];
        }
    }

    /* O no original fica apenas com as t-1 chaves menores. */
    y->n = t - 1;

    /* Abre um espaco no vetor de filhos do pai para inserir z. */
    for (int j = pai->n; j >= pos + 1; j--) {
        pai->filhos[j + 1] = pai->filhos[j];
    }
    pai->filhos[pos + 1] = z->id;

    /* Abre um espaco no vetor de chaves do pai para a mediana. */
    for (int j = pai->n - 1; j >= pos; j--) {
        pai->chaves[j + 1] = pai->chaves[j];
    }
    pai->chaves[pos] = y->chaves[t - 1];
    pai->n++;

    /* Cada mudanca precisa persistir nos arquivos correspondentes. */
    gravaNo(y);
    gravaNo(z);
    gravaNo(pai);

    liberaNo(z);
    liberaNo(y);
    liberaNo(pai);
}

/* Insere k em uma subarvore cuja raiz x nao esta cheia. */
static void insereNaoCheio(IdNo idNo, int k) {
    NoB *x = leNo(idNo);
    int i = x->n - 1;

    if (x->folha) {
        /* Desloca para a direita as chaves maiores do que k. */
        while (i >= 0 && k < x->chaves[i]) {
            x->chaves[i + 1] = x->chaves[i];
            i--;
        }

        x->chaves[i + 1] = k;
        x->n++;
        gravaNo(x);
        liberaNo(x);
        return;
    }

    /* Descobre qual filho corresponde ao intervalo de k. */
    while (i >= 0 && k < x->chaves[i]) {
        i--;
    }
    i++;

    NoB *filho = leNo(x->filhos[i]);
    bool filhoCheio = (filho->n == maxChaves());
    liberaNo(filho);

    if (filhoCheio) {
        /* CLRS divide o filho cheio antes de descer para ele. */
        liberaNo(x);
        divideFilho(idNo, i);

        /* O pai mudou: rele e compare k com a mediana que subiu. */
        x = leNo(idNo);
        if (k > x->chaves[i]) {
            i++; /* k pertence ao filho a direita da mediana. */
        }
    }

    IdNo proxId = x->filhos[i];
    liberaNo(x);
    insereNaoCheio(proxId, k);
}

static bool insereArv(int k) {
    if (buscaArv(k, NULL, NULL)) {
        return false; /* Politica deste programa: nao aceitar duplicatas. */
    }

    if (arv.raizId == ID_NULO) {
        NoB *raiz = criaNo(1);
        raiz->chaves[0] = k;
        raiz->n = 1;
        arv.raizId = raiz->id;
        gravaNo(raiz);
        gravaMeta();
        liberaNo(raiz);
        return true;
    }

    NoB *raiz = leNo(arv.raizId);
    bool raizCheia = (raiz->n == maxChaves());
    IdNo idRaizAnt = raiz->id;
    liberaNo(raiz);

    if (raizCheia) {
        /* A raiz cheia exige uma nova raiz antes do split. */
        NoB *raizNova = criaNo(0);
        raizNova->filhos[0] = idRaizAnt;
        arv.raizId = raizNova->id;
        gravaNo(raizNova);
        gravaMeta();
        IdNo idRaizNova = raizNova->id;
        liberaNo(raizNova);

        divideFilho(idRaizNova, 0);
        insereNaoCheio(idRaizNova, k);
    } else {
        insereNaoCheio(idRaizAnt, k);
    }

    return true;
}

/* ---------- Remocao CLRS: funcoes auxiliares ---------- */

static int buscaPos(const NoB *no, int k) {
    int pos = 0;
    while (pos < no->n && no->chaves[pos] < k) {
        pos++;
    }
    return pos;
}

static void removeFolha(IdNo idNo, int pos) {
    NoB *no = leNo(idNo);

    for (int i = pos + 1; i < no->n; i++) {
        no->chaves[i - 1] = no->chaves[i];
    }

    no->n--;
    gravaNo(no);
    liberaNo(no);
}

/* Retorna a maior chave da subarvore com raiz idNo. */
static int buscaPred(IdNo idNo) {
    NoB *no = leNo(idNo);

    while (!no->folha) {
        IdNo prox = no->filhos[no->n];
        liberaNo(no);
        no = leNo(prox);
    }

    int resultado = no->chaves[no->n - 1];
    liberaNo(no);
    return resultado;
}

/* Retorna a menor chave da subarvore com raiz idNo. */
static int buscaSuc(IdNo idNo) {
    NoB *no = leNo(idNo);

    while (!no->folha) {
        IdNo prox = no->filhos[0];
        liberaNo(no);
        no = leNo(prox);
    }

    int resultado = no->chaves[0];
    liberaNo(no);
    return resultado;
}

/*
 * Empresta do irmao esquerdo:
 * - a chave separadora do pai desce para o inicio do filho;
 * - a ultima chave do irmao sobe para o pai;
 * - se os nos forem internos, o ultimo filho do irmao passa para o filho.
 */
static void emprestaAnt(IdNo idPai, int pos) {
    NoB *pai = leNo(idPai);
    NoB *filho = leNo(pai->filhos[pos]);
    NoB *irmao = leNo(pai->filhos[pos - 1]);

    /* Desloca as chaves do filho uma posicao a direita. */
    for (int i = filho->n - 1; i >= 0; i--) {
        filho->chaves[i + 1] = filho->chaves[i];
    }

    /* Para nos internos, abre espaco para um novo ponteiro em filho[0]. */
    if (!filho->folha) {
        for (int i = filho->n; i >= 0; i--) {
            filho->filhos[i + 1] = filho->filhos[i];
        }
        filho->filhos[0] = irmao->filhos[irmao->n];
    }

    filho->chaves[0] = pai->chaves[pos - 1];
    pai->chaves[pos - 1] = irmao->chaves[irmao->n - 1];

    filho->n++;
    irmao->n--;

    gravaNo(filho);
    gravaNo(irmao);
    gravaNo(pai);

    liberaNo(filho);
    liberaNo(irmao);
    liberaNo(pai);
}

/*
 * Empresta do irmao direito, simetrico ao caso anterior.
 * A primeira chave do irmao sobe para o pai e a chave separadora desce.
 */
static void emprestaProx(IdNo idPai, int pos) {
    NoB *pai = leNo(idPai);
    NoB *filho = leNo(pai->filhos[pos]);
    NoB *irmao = leNo(pai->filhos[pos + 1]);

    filho->chaves[filho->n] = pai->chaves[pos];

    /* O primeiro filho do irmao acompanha a chave se os nos forem internos. */
    if (!filho->folha) {
        filho->filhos[filho->n + 1] = irmao->filhos[0];
    }

    pai->chaves[pos] = irmao->chaves[0];

    /* Remove a primeira chave do irmao deslocando as restantes para a esquerda. */
    for (int i = 1; i < irmao->n; i++) {
        irmao->chaves[i - 1] = irmao->chaves[i];
    }

    /* Remove tambem o primeiro ponteiro, quando o irmao e interno. */
    if (!irmao->folha) {
        for (int i = 1; i <= irmao->n; i++) {
            irmao->filhos[i - 1] = irmao->filhos[i];
        }
    }

    filho->n++;
    irmao->n--;

    gravaNo(filho);
    gravaNo(irmao);
    gravaNo(pai);

    liberaNo(filho);
    liberaNo(irmao);
    liberaNo(pai);
}

/*
 * Funde filho[pos], a chave separadora do pai e filho[pos+1].
 * O ID do filho esquerdo e preservado; o arquivo do filho direito e removido.
 */
static void fundeFilhos(IdNo idPai, int pos) {
    NoB *pai = leNo(idPai);
    NoB *esq = leNo(pai->filhos[pos]);
    NoB *dir = leNo(pai->filhos[pos + 1]);
    IdNo idDir = dir->id;
    int qtdEsqAnt = esq->n;

    /* A chave que separava os filhos desce para o no combinado. */
    esq->chaves[qtdEsqAnt] = pai->chaves[pos];

    /* Anexa as chaves do filho direito. */
    for (int i = 0; i < dir->n; i++) {
        esq->chaves[qtdEsqAnt + 1 + i] = dir->chaves[i];
    }

    /* Em nos internos, anexa tambem todos os filhos do lado direito. */
    if (!esq->folha) {
        for (int i = 0; i <= dir->n; i++) {
            esq->filhos[qtdEsqAnt + 1 + i] = dir->filhos[i];
        }
    }

    esq->n = qtdEsqAnt + dir->n + 1;

    /* Retira do pai a chave separadora. */
    for (int i = pos + 1; i < pai->n; i++) {
        pai->chaves[i - 1] = pai->chaves[i];
    }

    /* Retira do pai a referencia ao no direito absorvido. */
    for (int i = pos + 2; i <= pai->n; i++) {
        pai->filhos[i - 1] = pai->filhos[i];
    }
    pai->n--;

    gravaNo(esq);
    gravaNo(pai);

    liberaNo(esq);
    liberaNo(dir);
    liberaNo(pai);
    apagaArqNo(idDir);
}

/*
 * Garante que o filho escolhido tenha pelo menos t chaves antes de descer.
 * Primeiro tenta emprestar de um irmao; se isso nao for possivel, faz merge.
 * Retorna o indice correto do filho depois da reorganizacao.
 */
static int preparaFilho(IdNo idPai, int pos) {
    NoB *pai = leNo(idPai);
    int n = pai->n;
    IdNo idEsq = (pos > 0) ? pai->filhos[pos - 1] : ID_NULO;
    IdNo idDir = (pos < n) ? pai->filhos[pos + 1] : ID_NULO;
    int qtdEsq = -1;
    int qtdDir = -1;

    if (idEsq != ID_NULO) {
        NoB *esq = leNo(idEsq);
        qtdEsq = esq->n;
        liberaNo(esq);
    }
    if (idDir != ID_NULO) {
        NoB *dir = leNo(idDir);
        qtdDir = dir->n;
        liberaNo(dir);
    }
    liberaNo(pai);

    /* Caso 3(a): o irmao esquerdo tem uma chave sobrando. */
    if (qtdEsq >= arv.t) {
        emprestaAnt(idPai, pos);
        return pos;
    }

    /* Caso 3(a): senao, tenta emprestar do irmao direito. */
    if (qtdDir >= arv.t) {
        emprestaProx(idPai, pos);
        return pos;
    }

    /* Caso 3(b): nenhum irmao pode emprestar, portanto fazemos merge. */
    if (pos < n) {
        fundeFilhos(idPai, pos);
        return pos;
    }

    /* O filho escolhido e o ultimo; funde com o irmao da esquerda. */
    fundeFilhos(idPai, pos - 1);
    return pos - 1;
}

/* Declaracao antecipada: as funcoes de remocao chamam-se recursivamente. */
static void removeRec(IdNo idNo, int k);

/* Caso 2: remove uma chave que esta em um no interno. */
static void removeInterno(IdNo idNo, int pos) {
    NoB *no = leNo(idNo);
    int k = no->chaves[pos];
    IdNo idEsq = no->filhos[pos];
    IdNo idDir = no->filhos[pos + 1];

    NoB *esq = leNo(idEsq);
    NoB *dir = leNo(idDir);
    int qtdEsq = esq->n;
    int qtdDir = dir->n;
    liberaNo(esq);
    liberaNo(dir);

    if (qtdEsq >= arv.t) {
        /* Caso 2(a): predecessor, pois o filho esquerdo tem pelo menos t chaves. */
        int predecessor = buscaPred(idEsq);
        no->chaves[pos] = predecessor;
        gravaNo(no);
        liberaNo(no);
        removeRec(idEsq, predecessor);
    } else if (qtdDir >= arv.t) {
        /* Caso 2(b): o esquerdo esta no minimo, mas o direito tem pelo menos t. */
        int sucessor = buscaSuc(idDir);
        no->chaves[pos] = sucessor;
        gravaNo(no);
        liberaNo(no);
        removeRec(idDir, sucessor);
    } else {
        /* Caso 2(c): ambos estao no minimo; funde e remove k do no combinado. */
        liberaNo(no);
        fundeFilhos(idNo, pos);
        removeRec(idEsq, k);
    }
}

/*
 * Remove recursivamente k de uma subarvore. A funcao segue a propriedade
 * top-down do CLRS: antes de descer para um filho com t-1 chaves, reorganiza
 * a arvore para que esse filho passe a ter pelo menos t chaves.
 */
static void removeRec(IdNo idNo, int k) {
    NoB *no = leNo(idNo);
    int pos = buscaPos(no, k);

    /* A chave esta neste no. */
    if (pos < no->n && no->chaves[pos] == k) {
        if (no->folha) {
            /* Caso 1: basta retirar a chave da folha. */
            liberaNo(no);
            removeFolha(idNo, pos);
        } else {
            /* Caso 2: chave num no interno. */
            liberaNo(no);
            removeInterno(idNo, pos);
        }
        return;
    }

    /* A chave nao esta neste no e nao ha filhos para continuar. */
    if (no->folha) {
        liberaNo(no);
        return;
    }

    IdNo idFilho = no->filhos[pos];
    NoB *filho = leNo(idFilho);
    bool filhoMin = (filho->n == arv.t - 1);
    liberaNo(filho);
    liberaNo(no);

    if (filhoMin) {
        /* Empresta ou funde antes de continuar a busca nesse caminho. */
        pos = preparaFilho(idNo, pos);
        no = leNo(idNo); /* O pai pode ter mudado no preparaFilho. */
        idFilho = no->filhos[pos];
        liberaNo(no);
    }

    removeRec(idFilho, k);
}

static bool removeArv(int k) {
    if (arv.raizId == ID_NULO || !buscaArv(k, NULL, NULL)) {
        return false; /* A chave nao existe; a arvore permanece inalterada. */
    }

    IdNo idRaizAnt = arv.raizId;
    removeRec(idRaizAnt, k);

    NoB *raiz = leNo(arv.raizId);
    if (raiz->n == 0) {
        /* Uma raiz vazia e folha significa arvore vazia. */
        if (raiz->folha) {
            arv.raizId = ID_NULO;
            liberaNo(raiz);
            apagaArqNo(idRaizAnt);
        } else {
            /* Se a raiz perdeu sua ultima chave, seu unico filho vira raiz. */
            IdNo idRaizNova = raiz->filhos[0];
            arv.raizId = idRaizNova;
            liberaNo(raiz);
            apagaArqNo(idRaizAnt);
        }
        gravaMeta();
    } else {
        liberaNo(raiz);
    }

    return true;
}

/* ---------- Impressao visual por niveis ---------- */

static void enfileiraId(IdNo **fila, size_t *cap, size_t *fimFila, IdNo id) {
    if (*fimFila == *cap) {
        size_t novaCap = (*cap == 0) ? 16 : (*cap * 2);
        IdNo *novaFila = realloc(*fila, novaCap * sizeof **fila);
        if (novaFila == NULL) {
            free(*fila);
            erroFatal("memoria insuficiente para imprimir a arvore");
        }
        *fila = novaFila;
        *cap = novaCap;
    }
    (*fila)[(*fimFila)++] = id;
}

/* Percorre a arvore em largura e imprime os nos separados por nivel. */
static void imprimeArv(void) {
    if (arv.raizId == ID_NULO) {
        printf("Arvore B vazia.\n");
        return;
    }

    IdNo *fila = NULL;
    size_t cap = 0;
    size_t iniFila = 0;
    size_t fimFila = 0;
    int nivel = 1;

    enfileiraId(&fila, &cap, &fimFila, arv.raizId);

    while (iniFila < fimFila) {
        size_t qtdNivel = fimFila - iniFila;
        printf("Nivel %d: ", nivel++);

        for (size_t j = 0; j < qtdNivel; j++) {
            IdNo id = fila[iniFila++];
            NoB *no = leNo(id);

            printf("#%" PRIu64 " [", no->id);
            for (int i = 0; i < no->n; i++) {
                printf("%d", no->chaves[i]);
                if (i + 1 < no->n) {
                    printf(" | ");
                }
            }
            printf("]  ");

            if (!no->folha) {
                for (int i = 0; i <= no->n; i++) {
                    if (no->filhos[i] != ID_NULO) {
                        enfileiraId(&fila, &cap, &fimFila, no->filhos[i]);
                    }
                }
            }

            liberaNo(no);
        }

        putchar('\n');
    }

    free(fila);
}

/* ---------- Entrada e menu ---------- */

/* Le um inteiro por linha, evitando problemas com o buf do teclado. */
static bool leInt(const char *pergunta, int *valor) {
    char linha[TAM_ENTRADA];

    for (;;) {
        printf("%s", pergunta);
        if (fgets(linha, sizeof linha, stdin) == NULL) {
            return false; /* EOF: permite encerrar sem ficar preso no menu. */
        }

        errno = 0;
        char *fim = NULL;
        long valorLido = strtol(linha, &fim, 10);

        if (fim == linha || errno == ERANGE || valorLido < INT_MIN || valorLido > INT_MAX) {
            printf("Entrada invalida. Digite um numero inteiro.\n");
            continue;
        }

        while (*fim == ' ' || *fim == '\t' || *fim == '\r' || *fim == '\n') {
            fim++;
        }

        if (*fim != '\0') {
            printf("Entrada invalida. Digite apenas um numero inteiro.\n");
            continue;
        }

        *valor = (int)valorLido;
        return true;
    }
}

static void novaArv(void) {
    int t = 0;

    limpaArqArv();
    arv.raizId = ID_NULO;
    arv.proxId = 1;

    for (;;) {
        if (!leInt("Informe o grau minimo t (t >= 2): ", &t)) {
            exit(EXIT_SUCCESS);
        }

        if (t >= 2 && t <= INT_MAX / 2) {
            break;
        }

        printf("O grau deve ser pelo menos 2 e nao pode ser grande demais.\n");
    }

    arv.t = t;
    gravaMeta();
    printf("Nova arvore criada. Maximo de chaves por no: %d.\n", maxChaves());
}

int main(void) {
    if (temMeta()) {
        int escolha = 0;
        printf("Foi encontrado um arquivo de metadados de uma arvore B.\n");
        printf("1 - Continuar a arvore existente\n");
        printf("2 - Criar uma arvore nova (apaga os arquivos conhecidos da arvore anterior)\n");

        for (;;) {
            if (!leInt("Escolha: ", &escolha)) {
                return EXIT_SUCCESS;
            }
            if (escolha == 1 || escolha == 2) {
                break;
            }
            printf("Escolha 1 ou 2.\n");
        }

        if (escolha == 1) {
            leMeta();
            printf("Arvore carregada. t = %d; raiz ID = %" PRIu64 ".\n", arv.t, arv.raizId);
        } else {
            /* Carrega os metadados antigos para remover seus arquivos e reiniciar. */
            novaArv();
        }
    } else {
        novaArv();
    }

    for (;;) {
        int opcao = 0;
        printf("\n=== ARVORE B ===\n");
        printf("1 - Buscar chave\n");
        printf("2 - Inserir chave\n");
        printf("3 - Remover chave\n");
        printf("4 - Imprimir arvore por niveis\n");
        printf("5 - Mostrar grau e quantidade maxima de chaves\n");
        printf("0 - Sair\n");

        if (!leInt("Opcao: ", &opcao)) {
            break;
        }

        if (opcao == 1) {
            int chave = 0;
            if (!leInt("Chave para buscar: ", &chave)) {
                break;
            }
            IdNo idNo = ID_NULO;
            int pos = -1;
            if (buscaArv(chave, &idNo, &pos)) {
                printf("Chave %d encontrada no no #%" PRIu64 ", indice C %d.\n",
                       chave, idNo, pos);
            } else {
                printf("Chave %d nao encontrada.\n", chave);
            }
        } else if (opcao == 2) {
            int chave = 0;
            if (!leInt("Chave para inserir: ", &chave)) {
                break;
            }
            if (insereArv(chave)) {
                printf("Chave %d inserida.\n", chave);
            } else {
                printf("A chave %d ja existe; duplicatas nao sao aceitas.\n", chave);
            }
        } else if (opcao == 3) {
            int chave = 0;
            if (!leInt("Chave para remover: ", &chave)) {
                break;
            }
            if (removeArv(chave)) {
                printf("Chave %d removida.\n", chave);
            } else {
                printf("Chave %d nao encontrada; nenhuma alteracao realizada.\n", chave);
            }
        } else if (opcao == 4) {
            imprimeArv();
        } else if (opcao == 5) {
            printf("Grau minimo t = %d\n", arv.t);
            printf("Minimo de chaves por no nao-raiz = %d\n", arv.t - 1);
            printf("Maximo de chaves por no = %d\n", maxChaves());
            printf("Maximo de filhos por no interno = %d\n", maxFilhos());
        } else if (opcao == 0) {
            break;
        } else {
            printf("Opcao invalida.\n");
        }
    }

    printf("Dados mantidos nos arquivos binarios. Encerrando.\n");
    return EXIT_SUCCESS;
}
