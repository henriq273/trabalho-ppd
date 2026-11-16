#include <iostream>
#include <sstream>

using namespace std;

/*
 * Calcula a soma harmônica H(n) = 1/1 + 1/2 + ... + 1/n com d casas decimais,
 * usando apenas aritmética de inteiros (divisão longa, dígito a dígito).
 *
 * output: buffer que recebe o resultado como string "I.ddd...d" (terminada em '\0')
 * d:      número de casas decimais desejadas
 * n:      quantidade de termos da série
 *
 * Complexidade: O(n * d) no pior caso.
 */
void sum(char* output, const long unsigned int d, const long unsigned int n) {
	// digits[0]        -> parte inteira
	// digits[1..d]     -> casas decimais pedidas
	// digits[d+1..d+10] -> 10 dígitos de guarda, que absorvem o erro de
	//                      truncamento acumulado dos n termos
	// (VLA: extensão do GCC/Clang, não é C++ padrão)
	long unsigned int digits[d + 11];

	// Zera o acumulador de dígitos
	for (long unsigned int digit = 0; digit < d + 11; ++digit) {
		digits[digit] = 0;
	}

	// Laço externo: soma o termo 1/i para cada denominador i = 1..n
	for (long unsigned int i = 1; i <= n; ++i) {
		// Numerador de 1/i; depois, resto corrente da divisão longa
		long unsigned int remainder = 1;

		// Laço interno: divisão longa de 1/i, gerando um dígito por posição.
		// Para cedo (&& remainder) quando a divisão é exata (resto 0),
		// pois os dígitos seguintes seriam todos zero.
		for (long unsigned int digit = 0; digit < d + 11 && remainder; ++digit) {
			long unsigned int div = remainder / i; // dígito gerado nesta posição
			long unsigned int mod = remainder % i; // resto da divisão
			// O dígito é SOMADO (não atribuído): a posição pode passar de 9
			// durante o acúmulo; o carry é resolvido depois.
			digits[digit] += div;
			remainder = mod * 10; // "desce um zero", como na divisão manual
		}
	}

	// 1ª propagação de carry, da direita (menos significativo) para a esquerda.
	// Deixa em cada posição só um dígito (%10) e passa o excesso (/10) à
	// posição anterior. digits[0] (parte inteira) não é reduzido.
	for (long unsigned int i = d + 11 - 1; i > 0; --i) {
		digits[i - 1] += digits[i] / 10;
		digits[i] %= 10;
	}

	// Arredondamento: se o primeiro dígito descartado for >= 5, soma 1 na
	// última casa mantida (half-up aproximado; dígitos seguintes são ignorados).
	if (digits[d + 1] >= 5) {
		++digits[d];
	}

	// 2ª propagação de carry, só até a casa d: resolve o carry que o
	// arredondamento pode ter criado (ex.: ...999 + 1).
	for (long unsigned int i = d; i > 0; --i) {
		digits[i - 1] += digits[i] / 10;
		digits[i] %= 10;
	}

	// Formatação da saída
	stringstream stringstreamA;
	stringstreamA << digits[0] << "."; // parte inteira e ponto decimal
	// Casas decimais, uma a uma
	for (long unsigned int i = 1; i <= d; ++i) {
		stringstreamA << digits[i];
	}
	// NUL explícito: string::copy não coloca terminador; este '\0' entra em
	// size() e é copiado, terminando a string em output.
	stringstreamA << '\0';
	string stringA = stringstreamA.str();
	stringA.copy(output, stringA.size());
}

int main() {

	// d: casas decimais; n: número de termos da série
	long unsigned int d, n;

	// Lê "d n" da entrada padrão (inteiros sem sinal)
	cin >> d >> n;

	// Buffer de saída: precisa de d + 3 bytes (inteiro, '.', d dígitos, '\0');
	// d + 10 dá folga
	char output[d + 10]; // extra precision due to possible error

	sum(output, d, n);

	cout << output << endl;

	return 0;
}
