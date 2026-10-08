# Lista de tarefas

## 1. Melhorar consistência do resultado

    - Os resultados das somas e em diferentes execuções não são consistentes, mas divergem.
    - Melhorar controle do erro numérico.

## 2. Revisar uso de `reduction(+:sum)`

    - A cláusula não garante ordem de combinação, permitindo que o último dígito possa oscilar entre execuções diferentes.
    - Explorar alternativas.

## 3. Alternativas para `N` pequeno

    - Com N pequeno demais, a execução paralela gera overhead e desperdiça performance.
    - Identificar e implementar um limiar como condicional para a execução paralela e montar alternativas.

## 4. Revisar overflow de índice

    - Para `N` grande, o índice `i` pode exceder o limite de tipo e estourar silenciosamente.
    - Identificar se isso ocorre e quando ocorre, e como lidar com isso.

## 5. Identificar uso de `-ffast-math`

    - O GCC utiliza `-ffast-math` por padrão, mas o Clang não. Essa flag autoriza o compilador a reassociar somas e trocar a operação por uma aproximação de recíproco. Gera ganho de desempenho mas pode perder precisão.
    - Identificar e corrigir, ou dividir num par de alternativas (referente à precisão desejada, por exemplo).

## 6. Melhorar o trabalho com os cronômetros