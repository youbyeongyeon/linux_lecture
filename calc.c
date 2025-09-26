#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	int a = atoi(argv[1]);
	char op = argv[2][0];
	int b = atoi(argv[3]);
	int result;
	
	switch(op){
		case '+':
			result = a + b;
			break;
		case '-':
			result = a - b;
			break;
		case 'x':
			result = a * b;
			break;
		case '/':
			result = a / b;
			break;
	}

	printf("%d\n", result);
	return 0;

}

