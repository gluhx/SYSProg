#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

char * read_string() {
	char c; //текущий символ
	int length = 0; //длина строки
	
	//выделяем память начально
	char * str = malloc(length * sizeof(char));

	//считываем до конца строки
	while ((c = getchar()) != EOF && c != '\n'){
		
		//проверяем печытный ли это символ
		if (isprint((unsigned char)c) || c == ' ' || c == '\t') {
			
			//выделяем память для масива
			length ++;
			str = realloc(str, length * sizeof(char));
			
			//проверяем выделение памяти
			if (str == NULL) {
				printf("Ошибка выделения памяти");
				return NULL;
			}

			//записываем элемент
			str[length - 1] = c;
		}
	}

	// добавляем конец строки
	length ++;
	str = realloc(str, length * sizeof(char));	
	str[length - 1] = '\0';

	return str;
}

char random_key() {
	// инициализируем генераторс случайных чисел
	srand(time(NULL));

	//генерируем ключ
	return (char)(rand() % 95 + 32); 
}

char * xor(const char* str, char key) {
	//проверяем массив
	if (str == NULL) {
        	return NULL;
    	}

	//проверяем размер массива
    	int length = strlen(str);
    	if (length == 0) {
        	return NULL;
    	}

    	// выделяем память для результата
    	char* result = malloc((length + 1) * sizeof(char));
    	if (result == NULL) {
        	printf("Ошибка выделения памяти для результата\n");
        	return NULL;
    	}

    	// выполняем XOR
    	for (int i = 0; i < length; i++) {
        	result[i] = str[i] ^ key;
    	}

    	result[length] = '\0';
    	return result;
}

int main() {
	//генерируем ключ
	char key = random_key();

	//читаем строку
	printf("Случайный ключ: %c\n", key);
	char *str = read_string();

	
	//шифруем строку
	char * result = xor(str,key);

	//выводим результат
	char c;	
	int i = 0;
	while ((c = result[i++]) != '\0'){
		printf("%c ", c);
	}
	
	printf("%c", '\n');

	i = 0;
	while ((c = result[i++]) != '\0'){
		printf("%d ", (unsigned char)c);
	}
}

