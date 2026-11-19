#!/bin/bash
# job_mpi.sh - modelo para rodar UM programa MPI em C no cluster openhpc (UFSCar).
#
# Coloque na MESMA pasta: este script, o seu .c e o(s) arquivo(s) de entrada.
# Edite so o bloco "CONFIGURE AQUI" e, se quiser, -N / --ntasks-per-node / -t.
#
#   bash job_mpi.sh          # compila AQUI e poe na fila (NAO use 'sbatch job_mpi.sh')
#   squeue -u $USER          # PD = na fila, R = rodando, sumiu da lista = terminou
#   cat saida-<numero>.out
#
# Por que 'bash' e nao 'sbatch': os nos de computacao nao tem os cabecalhos do C
# (stdio.h etc.), entao compilar dentro do job falha. O script compila no no de
# login e so depois chama o sbatch sozinho.
#
# Nao precisa de container (Singularity): gcc 9.4 + OpenMPI 4.1.1 ja vem
# carregados no cluster.

#SBATCH -J mpi-grupo               # nome que aparece no squeue
#SBATCH -o saida-%j.out            # %j vira o numero do job
#SBATCH -p normal,slow             # roda na que liberar primeiro (as duas tem nos de 40 CPUs)
#SBATCH --exclude=c23              # c23 tem 96 CPUs e os outros 40: misturar desbalanceia
#SBATCH -N 2                       # numero de MAQUINAS (2 = memoria distribuida de verdade)
#SBATCH --ntasks-per-node=1        # 1 rank MPI por maquina (o paralelismo dentro dela e do OpenMP).
#                                    Total de ranks = N x este valor = N
#SBATCH --cpus-per-task=2          # NAO MUDE: cada nucleo fisico tem 2 "CPUs" (hyperthreading).
#                                    O rank reserva 2 CPUs; o --threads fica no entrada.in
#SBATCH -t 00:15:00                # tempo maximo. Quanto menor, mais cedo sai da fila
##SBATCH --mem-per-cpu=1G          # padrao e 500 MB por processo. Tire um '#' se der oom-kill
#
# Aviso por e-mail: tire um '#' das duas linhas abaixo. ATENCAO: em 13/10/2026 o
# servidor de e-mail do cluster (postfix) estava desligado - teste antes de contar com isso.
##SBATCH --mail-type=END,FAIL
##SBATCH --mail-user=SEU_EMAIL@estudante.ufscar.br

# ============================ CONFIGURE AQUI ============================
FONTE=harmonic-sum.c     # seu codigo-fonte
ARQUIVO_ARGUMENTOS=entrada.in   # arquivo com os argumentos do programa (uma linha), ex.:
                                # --size=100000 --precision=20 --threads=2
ARGUMENTOS="$(cat "$ARQUIVO_ARGUMENTOS" 2>/dev/null)"  # lidos dele (a fase 1 confere se existe)
ENTRADA_STDIN=""         # so se o programa le com scanf/stdin (./prog < arquivo):
                         # coloque o nome do arquivo aqui. Se o programa abre o
                         # arquivo sozinho com fopen(), deixe "" - basta o
                         # arquivo estar nesta pasta.
# =======================================================================

BIN="${FONTE%.c}"

# ------------------------------------------------------------------------
# Fase 1 - no de login ('bash job_mpi.sh'): confere, compila, poe na fila.
# ------------------------------------------------------------------------
if [ -z "$SLURM_JOB_ID" ]; then
  cd "$(dirname "$0")" || exit 1
  SCRIPT=$(basename "$0")

  if [ ! -f "$FONTE" ]; then
    echo "ERRO: '$FONTE' nao esta em $PWD"; exit 1
  fi
  if [ -n "$ARQUIVO_ARGUMENTOS" ] && [ ! -f "$ARQUIVO_ARGUMENTOS" ]; then
    echo "ERRO: arquivo de argumentos '$ARQUIVO_ARGUMENTOS' nao esta em $PWD"; exit 1
  fi
  if [ -n "$ENTRADA_STDIN" ] && [ ! -f "$ENTRADA_STDIN" ]; then
    echo "ERRO: entrada '$ENTRADA_STDIN' nao esta em $PWD"; exit 1
  fi

  echo "compilando $FONTE ..."
  # -fopenmp e obrigatorio: sem ele os #pragma omp sao ignorados e cada rank roda com 1 thread.
  # Sem -march=native: o no de login pode ter CPU diferente dos nos de computacao.
  mpicc -O2 -fopenmp -o "$BIN" "$FONTE" -lm || { echo "ERRO: compilacao falhou. Nada foi para a fila."; exit 1; }
  sbatch "$SCRIPT"
  exit $?
fi

# ------------------------------------------------------------------------
# Fase 2 - no de computacao (o Slurm executa esta parte).
# ------------------------------------------------------------------------
cd "$SLURM_SUBMIT_DIR" || exit 1

# OBRIGATORIO: na rede 192.168.22.x um no nao consegue abrir conexao TCP com
# outro, e e essa que o MPI escolhe sozinho - ele trava na primeira mensagem
# entre maquinas. A rede 200.18.104.x funciona (testado em 15/10/2026).
export OMPI_MCA_btl_tcp_if_include=200.18.104.0/24
export OMPI_MCA_oob_tcp_if_include=200.18.104.0/24

# Pilha para precisoes grandes (--precision): as copias privadas do reduction
# ((d+11)*8 bytes por thread) ficam na pilha. OMP_STACKSIZE vale para as threads
# trabalhadoras; a thread mestre usa a pilha do processo (ulimit -s, padrao 8 MB).
export OMP_STACKSIZE=64M
ulimit -s unlimited 2>/dev/null || ulimit -s "$(ulimit -H -s)"

# Como o mpirun e chamado (usado nas tres chamadas abaixo):
#  --map-by ppr:1:node : exatamente 1 rank por maquina; o paralelismo dentro da
#                        maquina e do OpenMP (--threads no entrada.in).
#  --bind-to none      : sem isso o OpenMPI prende cada rank a UM nucleo quando ha
#                        poucos ranks, e todas as threads OpenMP disputariam esse
#                        nucleo. (So desliga a fixacao do OpenMPI; a afinidade que o
#                        proprio Slurm aplica continua valendo - veja a secao
#                        "CPUs visiveis por rank" na saida.)
#  -x OMP_STACKSIZE    : o mpirun nao repassa essa variavel as outras maquinas sozinho.
MPIRUN="mpirun -n $SLURM_NTASKS --map-by ppr:1:node --bind-to none -x OMP_STACKSIZE"

echo "job=$SLURM_JOB_ID  maquinas=$SLURM_JOB_NODELIST  processos=$SLURM_NTASKS"
date
echo

if [ ! -x "$BIN" ] || [ "$FONTE" -nt "$BIN" ]; then
  echo "ERRO: o executavel '$BIN' nao existe ou e mais antigo que '$FONTE'."
  echo "      Envie com 'bash job_mpi.sh', nao 'sbatch job_mpi.sh': o bash compila antes."
  exit 1
fi

# Esperado: 1 processo por maquina. Se aparecer uma maquina so, rodou em memoria compartilhada.
echo "=== processos por maquina ==="
$MPIRUN hostname | sort | uniq -c
echo

# Quantas CPUs cada rank realmente enxerga (nproc respeita a afinidade do Slurm) e
# a pilha disponivel. Use para escolher o --threads do entrada.in: mais threads do
# que CPUs visiveis so causa oversubscription.
echo "=== CPUs visiveis por rank ==="
$MPIRUN bash -c 'echo "$(hostname): nproc=$(nproc) afinidade=$(taskset -cp $$ | cut -d: -f2) ulimit-s=$(ulimit -s)"' | sort
echo

echo "=== execucao: ./$BIN $ARGUMENTOS ==="
INICIO=$(date +%s.%N)
if [ -n "$ENTRADA_STDIN" ]; then
  # o MPI entrega o stdin so para o rank 0; ele que distribui para os outros
  $MPIRUN ./"$BIN" $ARGUMENTOS < "$ENTRADA_STDIN"
else
  $MPIRUN ./"$BIN" $ARGUMENTOS
fi
STATUS=$?
FIM=$(date +%s.%N)
echo

echo "=== fim: codigo de saida $STATUS, $(awk -v a="$INICIO" -v b="$FIM" 'BEGIN{printf "%.2f", b-a}') s no total ==="
date
exit $STATUS