#include "mmemory.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define TRUE			1
#define FALSE			0
#define ALLOCSIZE		128				/* объем имеющейся памяти (128 байт) */
#define NULL_SYMBOL		'\0'			/* символ инициализации памяти */
#define SWAPING_NAME	"swap.dat"		/* файл подкачки */

/* Коды возврата */
const int SUCCESSFUL_CODE				=  0;
const int INCORRECT_PARAMETERS_ERROR	= -1;
const int NOT_ENOUGH_MEMORY_ERROR		= -2;
const int OUT_OF_RANGE_ERROR			= -2;
const int UNKNOWN_ERROR					=  1;

/* Блок непрерывной памяти (занятой или свободный) */
struct block {
	struct block*	pNext;			// следующий свободный или занятый блок
	unsigned int	szBlock;		// размер блока	
	unsigned int	offsetBlock;	// смещение блока относительно страницы
};

/* Информация о странице в таблице страниц */
struct pageInfo {
	unsigned long	offsetPage;		// смещение страницы от начала памяти или файла
	char			isUse;			// флаг, описывающий находится ли страница в памяти или на диске
};

/* Заголовок страницы */
typedef struct page {
	struct block*	pFirstFree;			// первый свободный блок
	struct block*	pFirstUse;			// первый занятый блок
	unsigned int	maxSizeFreeBlock;	// макс. размер сводного блока	
	struct pageInfo	pInfo;				// информация о странице
	unsigned int	activityFactor;		// коэффициент активности
} page;

/* Таблица страниц */
struct page *pageTable; 

/* Количество страниц (в памяти и на диске) */
int numberOfPages = 0;

/* Размер страницы */
int sizePage = 0;

/* Физическая память */
VA allocbuf;

void ___print_memory();
int initPageTable();
void addToUsedBlocks(struct block *_block, struct page *_page);
void addToFreeBlocks(struct block *_block, struct page *_page);
int mallocInPage(VA* ptr, size_t szBlock, int pageNum);
void nullMemory();
unsigned int getMaxSizeFreeBlock(struct page _page);
struct page* getLeastActivePageInMemory();
int swap(struct page *memoryPage, struct page *swopPage);
int readf(unsigned long offsetPage, VA readPage);
int writef(unsigned long offsetPage, VA writtenPage);
VA convertVirtualtoPhysicalAddress(VA ptr, int *offsetPage, int *offsetBlock);
struct block* findBlock(int offsetPage, int offsetBlock);
struct block* findParentBlock(int offsetPage, int offsetBlock);

int _malloc (VA* ptr, size_t szBlock)
{
	int i;
	struct page *reservePageInSwap = NULL;
	int reservePageNum;

	if (!ptr || szBlock > (unsigned int) sizePage)
	{
		return INCORRECT_PARAMETERS_ERROR;
	}
	
	// обходим все страницы
	for (i = 0; i < numberOfPages; i++)
	{
		// если в странице есть блок достаточной свободной памяти
		if (pageTable[i].maxSizeFreeBlock >= szBlock)
		{
			// если эта страница в памяти, то нам повезло
			if (pageTable[i].pInfo.isUse)
			{
				return mallocInPage(ptr, szBlock, i);
			}
			else if (!reservePageInSwap)
			{
				// если эта страница в свопе, то запоминаем её
				// на случай, если в памяти подходящей страницы не будет найдено
				reservePageInSwap = &pageTable[i];
				reservePageNum = i;
			}
		}
	}
	// если свободная страница нашлась только в свопе
	if (reservePageInSwap)
	{
		int __errno = swap(getLeastActivePageInMemory(), reservePageInSwap);
		return mallocInPage(ptr, szBlock, reservePageNum);
	}
	return NOT_ENOUGH_MEMORY_ERROR;
}

int _free (VA ptr)
{
	VA raddr;
	int indexPage, offsetBlock;
	struct block *freeBlock, *parentBlock;
	if (!ptr)
	{
		return INCORRECT_PARAMETERS_ERROR;
	}
	raddr = convertVirtualtoPhysicalAddress(ptr, &indexPage, &offsetBlock);
	freeBlock = findBlock(indexPage, offsetBlock);
	parentBlock = findParentBlock(indexPage, offsetBlock);
	if (parentBlock)
	{
		parentBlock -> pNext = freeBlock -> pNext;
	}
	else
	{
		pageTable[indexPage].pFirstUse = freeBlock -> pNext;
	}
	addToFreeBlocks(freeBlock, &pageTable[indexPage]);
	return SUCCESSFUL_CODE;
	//return UNKNOWN_ERROR;
}

int _read (VA ptr, void* pBuffer, size_t szBuffer)
{
	VA raddr, vaBuffer;
	int indexPage, offsetBlock;
	unsigned int j;
	struct block* readBlock;
	if (!ptr || !pBuffer || (szBuffer > (unsigned int) sizePage))
	{
		return INCORRECT_PARAMETERS_ERROR;
	}
	raddr = convertVirtualtoPhysicalAddress(ptr, &indexPage, &offsetBlock);
	// если не получили адрес, то нужный блок в свопе
	// выполняем страничное прерывание
	if (!raddr)
	{
		int __errno = swap(getLeastActivePageInMemory(), &pageTable[indexPage]);
		raddr = convertVirtualtoPhysicalAddress(ptr, &indexPage, &offsetBlock);
	}
	//___print_memory();
	readBlock = findBlock(indexPage, offsetBlock);
	if (szBuffer <= readBlock -> szBlock)
	{
		VA raddr2;
		vaBuffer = (VA) pBuffer;
		raddr2 = convertVirtualtoPhysicalAddress(vaBuffer, &indexPage, &offsetBlock);

		//
		if (!raddr2)
		{
			int __errno = swap(getLeastActivePageInMemory(), &pageTable[indexPage]);
			raddr2 = convertVirtualtoPhysicalAddress(ptr, &indexPage, &offsetBlock);
		}
		//

		for (j = 0; j < szBuffer; j++)
			raddr2[j] = raddr[j];
			//vaBuffer[j] = raddr[j];
		pBuffer = (void*) vaBuffer;
		return SUCCESSFUL_CODE;
	}
	
	else
	{
		return OUT_OF_RANGE_ERROR;
	}	
}

int _write (VA ptr, void* pBuffer, size_t szBuffer)
{
	VA raddr, vaBuffer;
	int indexPage, offsetBlock;
	struct block* readBlock;
	unsigned int j;
	if (!ptr || !pBuffer || (szBuffer > (unsigned int) sizePage))
	{
		return INCORRECT_PARAMETERS_ERROR;
	}
	raddr = convertVirtualtoPhysicalAddress(ptr, &indexPage, &offsetBlock);
	if (!raddr)
	{
		int __errno = swap(getLeastActivePageInMemory(), &pageTable[indexPage]);
		raddr = convertVirtualtoPhysicalAddress(ptr, &indexPage, &offsetBlock);
	}
	readBlock = findBlock(indexPage, offsetBlock);
	if (szBuffer <= readBlock -> szBlock)
	{
		vaBuffer = (VA) pBuffer;
		for (j = 0; j < szBuffer; j++)
			raddr[j] = vaBuffer[j];
		pageTable[indexPage].activityFactor++;
		//___print_memory();
		return SUCCESSFUL_CODE;
	}
	
	else
	{
		return OUT_OF_RANGE_ERROR;
	}
	
}

int _init (int n, int szPage)
{	
	remove(SWAPING_NAME);
	if (n > 0 && szPage > 0)
	{
		sizePage = szPage;
		numberOfPages = n;
		pageTable = (page*) malloc(sizeof(page) * n);
		allocbuf = (VA) malloc(sizeof(char) * ALLOCSIZE);
		nullMemory();
		if (allocbuf && pageTable)
		{						
			return initPageTable();
		}
		else
		{			
			return UNKNOWN_ERROR;
		}
	}
	return INCORRECT_PARAMETERS_ERROR;
}

/**
 	@func	initPageTable
 	@brief	Инициализация таблицы страниц
	
	@return	код ошибки
	@retval	0	успешное выполнение
	@retval	1	неизвестная ошибка
 **/
int initPageTable()
{
	int numberOfSwapPages = 0;
	int numberOfRAMPages = 0;
	int __errno;
	int i;

	for (i = 0; i < numberOfPages; i++)
	{
		// изначально у каждой страницы есть единственный свободный блок с размером, равным размеру страницы
		// выделяем память под свободный блок и инициализируем его
		struct block* firstBlock = (struct block *) malloc(sizeof(struct block));
		firstBlock -> pNext = NULL;
		firstBlock -> szBlock = sizePage;
		firstBlock -> offsetBlock = 0;

		// инициализируем страницу
		pageTable[i].pFirstFree = firstBlock;
		pageTable[i].pFirstUse = NULL;
		pageTable[i].maxSizeFreeBlock = sizePage;				
		pageTable[i].activityFactor = 0;

		// если памяти хватает, то пишем страницу в память, иначе на диск
		if ((i+1) * sizePage <= ALLOCSIZE)
		{			
			pageTable[i].pInfo.offsetPage = numberOfRAMPages++;
			pageTable[i].pInfo.isUse = TRUE;
			// указатель на начало первого блока при инициализации равен физическому адресу страницы
			//pageTable[i].pFirstFree -> allocp = convertVirtualtoPhysicalAddress(pageTable[i].pInfo.offsetPage);
		}
		else
		{
			__errno = writef(numberOfSwapPages, allocbuf);	// ??? надо ли запоминать инициализированную память?????
			if (!__errno)
			{
				pageTable[i].pInfo.offsetPage = numberOfSwapPages++;
				pageTable[i].pInfo.isUse = FALSE;
				// т.к. уже вышли за пределы физической памяти, то устанавливаем указатель на начало памяти
				// при страничном прерывании он будет пересчитываться в зависимости от нового смещения страницы
				//pageTable[i].pFirstFree -> allocp = allocbuf;
			}
			else
			{
				return UNKNOWN_ERROR;
			}
		}
	}
	return SUCCESSFUL_CODE;
}

/**
 	@func	___print_memory
 	@brief	Вывести содержимое физической памяти на консоль (для тестирования)
 **/
void ___print_memory()
{
	int i;
	for (i = 0; i < ALLOCSIZE; i++)
	{
		printf("%c", allocbuf[i]);
	}
	printf("%c", '\n');
}

/**
 	@func	addToUsedBlocks
 	@brief	Добавляет блок в список занятых блоков указанной страницы
	@param	[in] _block			указатель на блок
	@param	[in] _page			указатель на страницу	
 **/
void addToUsedBlocks(struct block *_block, struct page *_page)
{
	struct block *usedBlockPtr = _page -> pFirstUse;
	struct block *parentBlockPtr = NULL;
	while (usedBlockPtr)
	{
		parentBlockPtr = usedBlockPtr;
		usedBlockPtr = usedBlockPtr -> pNext;
	}
	if (parentBlockPtr)
	{
		parentBlockPtr -> pNext = _block;
	}
	else
	{
		_page -> pFirstUse = _block;
	}
}

void addToFreeBlocks(struct block *_block, struct page *_page)
{
	struct block *freeBlockPtr = _page -> pFirstFree;
	struct block *parentBlockPtr = NULL;
	// находим крайний свободный блок
	while (freeBlockPtr)
	{
		parentBlockPtr = freeBlockPtr;
		freeBlockPtr = freeBlockPtr -> pNext;
	}
	// если нашли, то добавляем освобождённый блок в конец свободных блоков
	if (parentBlockPtr)
	{
		// если свободные блоки идут друг за другом, то их склеиваем
		if ((parentBlockPtr -> offsetBlock + parentBlockPtr -> szBlock)
			== _block -> offsetBlock)
		{
			parentBlockPtr -> szBlock += _block -> szBlock;
			free(freeBlockPtr);
		}
		else
		{
			parentBlockPtr -> pNext = _block;
			_block -> pNext = NULL;
		}
	}
	else
	{
		_page -> pFirstFree = _block;
	}
}

/**
 	@func	mallocInPage
 	@brief	Выделяет блок памяти определенного размера в определённой странице
	@param	[out] ptr		адресс блока
	@param	[in]  szBlock	размер блока
	@param	[in]  _page		адрес страницы
	
	@return	код ошибки
	@retval	0	успешное выполнение
	@retval	1	неизвестная ошибка
 **/
int mallocInPage(VA* ptr, size_t szBlock, int pageNum)
{
	struct page *_page = &pageTable[pageNum];
	struct block *blockPtr = _page -> pFirstFree;
	struct block *parentBlockPtr = NULL;
	// ищем подходящий блок
	while (blockPtr)
	{
		// если нашли подходящий по размеру блок
		if (blockPtr -> szBlock >= szBlock)
		{
			int vaddr = pageNum * sizePage + blockPtr -> offsetBlock + 1;
			*ptr = (VA) (vaddr);	// прибавляем единицу, чтобы указатель на первый блок не был нулевым
			if (blockPtr -> szBlock == szBlock)
			{
				// свободный блок заняли полностью
				if (parentBlockPtr)
					parentBlockPtr -> pNext = blockPtr -> pNext;
				else
					_page -> pFirstFree = NULL;
				blockPtr -> pNext = NULL;
				addToUsedBlocks(blockPtr, _page);
			}
			else
			{
				// создаём новый занятый блок
				struct block* usedBlock;
				usedBlock = (struct block *) malloc(sizeof(struct block));
				usedBlock -> offsetBlock = blockPtr -> offsetBlock;
				usedBlock -> pNext = NULL;
				usedBlock -> szBlock = szBlock;
				addToUsedBlocks(usedBlock, _page);

				// свободный блок сужается
				blockPtr -> offsetBlock += szBlock;
				blockPtr -> szBlock -= szBlock;				
			}
			// обновляем информацию о странице
			_page -> maxSizeFreeBlock = getMaxSizeFreeBlock(*_page);
			_page -> activityFactor++;
			return SUCCESSFUL_CODE;
		}
		else
		{
			// блок ещё не найден
			parentBlockPtr = blockPtr;
			blockPtr = blockPtr -> pNext;
		}
	}
	return UNKNOWN_ERROR;
}

/**
 	@func	nullMemory
 	@brief	Обнуляет память
 **/
void nullMemory()
{
	//memset(allocbuf, NULL_SYMBOL, ALLOCSIZE);
	int i;
	for (i = 0; i < ALLOCSIZE; i++)
	{
		allocbuf[i] = NULL_SYMBOL;
	}
}

/**
 	@func	getMaxSizeFreeBlock
 	@brief	Находит максимальную длину свободного блока в странице	
	@param	[in] _page			_страница	
	@return	максимальная длину свободного блока в странице	
 **/
unsigned int getMaxSizeFreeBlock(struct page _page)
{
	unsigned int maxSize = 0;
	struct block *blockPtr = _page.pFirstFree;
	while (blockPtr)
	{
		if (blockPtr -> szBlock > maxSize)
		{
			maxSize = blockPtr -> szBlock;
		}
		blockPtr = blockPtr -> pNext;
	}
	return maxSize;
}

/**
 	@func	getLeastActivePageInMemory
 	@brief	Находит наименее активную страницу в памяти
	@return	наименее активная страница
 **/
struct page* getLeastActivePageInMemory()
{	
	int i;	
	int minActivityIndex;
	struct page* minActivityPage = NULL;
	for (i = 0; i < numberOfPages; i++)
	{
		// если страница в памяти, и её коэффициент активности оказался меньше
		if (pageTable[i].pInfo.isUse)
		{
			if (!minActivityPage ||
				pageTable[i].activityFactor < minActivityPage -> activityFactor)
			{
				minActivityPage = &pageTable[i];
				minActivityIndex = i;
			}
		}
	}
	return minActivityPage;
}

/**
 	@func	convertVirtualtoPhysicalAddress
 	@brief	Концертация виртуального адреса в физический
	@param	[in] ptr			виртуальный адрес
	@param	[out] offsetPage	смещение страницы
	@param	[out] offsetBlock	смещение блока
	@return	физический адрес
 **/
VA convertVirtualtoPhysicalAddress(VA ptr, int *offsetPage, int *offsetBlock)
{
	struct block *trueBlock, *tempBlock;
	int maxBlockOffset;
	const int SIGN_DIFFERENCE = 256;
	int vaddr = (int)ptr - 1;		// отнимаем единицу, которую прибавили при выделении блока (чтобы указатель на первый блок не был нулевым)
	VA allocp = NULL;
	//int offset;
	if (vaddr < 0)
	{
		vaddr = SIGN_DIFFERENCE * sizeof(char) + vaddr; 
	}
	//offset = (int) (log((double)sizePage) / log(2.0));
	//offsetPage = vaddr >> offset;
	*offsetPage = vaddr / sizePage;
	*offsetBlock = vaddr % sizePage;
		
	if (*offsetPage <= numberOfPages && pageTable[*offsetPage].pInfo.isUse)
	{
		allocp = allocbuf + pageTable[*offsetPage].pInfo.offsetPage * sizePage + *offsetBlock;
	}

	tempBlock = pageTable[*offsetPage].pFirstUse;
	maxBlockOffset = tempBlock -> offsetBlock;
	while (tempBlock && tempBlock -> pNext && tempBlock -> pNext -> offsetBlock <= *offsetBlock)
		tempBlock = tempBlock -> pNext;
	*offsetBlock = tempBlock -> offsetBlock;
	return allocp;
}

/**
 	@func	findBlock
 	@brief	Находит блок с указанным смещением в указанной странице
	@param	[in] offsetPage	смещение страницы
	@param	[in] offsetBlock	смещение блока
	@return	указатель на искомый блок
 **/
struct block* findBlock(int offsetPage, int offsetBlock)
{
	struct block* blockPtr = pageTable[offsetPage].pFirstUse;
	while (blockPtr && blockPtr -> offsetBlock != offsetBlock)
	{
		blockPtr = blockPtr -> pNext;
	}
	return blockPtr;
}

struct block* findParentBlock(int offsetPage, int offsetBlock)
{
	struct block* parentBlock = NULL;
	struct block* blockPtr = pageTable[offsetPage].pFirstUse;
	while (blockPtr && blockPtr -> offsetBlock != offsetBlock)
	{
		parentBlock = blockPtr;
		blockPtr = blockPtr -> pNext;
	}
	return parentBlock;
}

/**
 	@func	swap
 	@brief	Процедура страничного прерывания
	@param	[in] memoryPage		страница в памяти
	@param	[in] swopPage		страница на диске
	@return	код ошибки
	@retval	0	успешное выполнение
	@retval	-1	страница на диске не найдена
	@retval	-2	неожиданный конец файла
 **/
int swap(struct page *memoryPage, struct page *swopPage)
{
	const int add_offset = 2;
	struct pageInfo bufInfo;
	int __errno, j;

	VA memoryPtr = allocbuf + memoryPage -> pInfo.offsetPage * sizePage - add_offset;
	VA memoryPageContent = (VA) malloc(sizeof(char) * sizePage);

	for (j = 0; j < sizePage; j++)
		memoryPageContent[j] = memoryPtr[j];
	__errno = readf(swopPage -> pInfo.offsetPage, memoryPtr);
	if (!__errno)
		__errno = writef(swopPage -> pInfo.offsetPage, memoryPageContent);

	bufInfo = memoryPage -> pInfo;
	memoryPage -> pInfo = swopPage -> pInfo;
	swopPage -> pInfo = bufInfo;
	memoryPage -> activityFactor++;
	swopPage -> activityFactor++;
	return __errno;
}

/**
 	@func	readf
 	@brief	Читает страницу из файла (свопинга)
	
	@param	[in]  offsetPage	смещение страницы в файле
	@param	[out] readPage		место для прочитанной страницы
	
	@return	код ошибки
	@retval	0	успешное выполнение
	@retval	-1	страница не найдена
	@retval	-2	неожиданный конец файла
 **/
int readf(unsigned long offsetPage, VA readPage)
{
	const int FILE_NOT_FOUND = -1;
	const int UNEXPECTED_EOF = -2;

	FILE *fp;
	int returnValue;
	long fileOffset;
	int __errno;

	fileOffset = offsetPage * sizePage;
	__errno = fopen_s(&fp, SWAPING_NAME, "r");
	if (!__errno)
	{
		__errno = fseek(fp, fileOffset, SEEK_SET);
		if (!__errno/* && fgets(readPage, sizePage + 1, fp)*/)
		{
			
			int i;
			for (i = 0; i < sizePage; i++)
				readPage[i] = fgetc(fp);
			returnValue = SUCCESSFUL_CODE;
		}
		else
		{
			returnValue = UNEXPECTED_EOF;
		}
		fclose(fp);
	}
	else
	{
		returnValue = FILE_NOT_FOUND;
	}
	return returnValue;
}

/**
 	@func	writef
 	@brief	Пишет страницу в файл (свопинг)
	
	@param	[in] offsetPage		смещение страницы в файле
	@param	[in] writtenPage	содержимое страницы
	
	@return	код ошибки
	@retval	0	успешное выполнение
	@retval	-1	страница не найдена
	@retval	-2	неожиданный конец файла
 **/
int writef(unsigned long offsetPage, VA writtenPage)
{
	const int FILE_NOT_FOUND = -1;
	const int UNEXPECTED_EOF = -2;

	FILE *fp;
	int returnValue;
	long fileOffset;
	int __errno;

	fileOffset = offsetPage * sizePage;
	__errno = fopen_s(&fp, SWAPING_NAME, "r+");		// открываем для модификации
	if (__errno == 2)
	{
		// если попали сюда, значит своп ещё не создан, создаём его
		__errno = fopen_s(&fp, SWAPING_NAME, "w");
	}
	if (!__errno)
	{
		__errno = fseek(fp, fileOffset, SEEK_SET);
		if (!__errno /*&& fputs(writtenPage, fp) >= 0*/)
		{
			int i;
			for (i = 0; i < sizePage; i++)
				fputc(writtenPage[i], fp);
			returnValue = SUCCESSFUL_CODE;
		}
		else
		{
			returnValue = UNEXPECTED_EOF;
		}
		fclose(fp);
	}
	else
	{
		returnValue = FILE_NOT_FOUND;
	}
	return returnValue;
}

void _printBuffer(VA ptr, size_t szBuffer)
{
	VA raddr, vaBuffer;
	int indexPage, offsetBlock;
	unsigned int j;
	struct block* readBlock;
	raddr = convertVirtualtoPhysicalAddress(ptr, &indexPage, &offsetBlock);
	// если не получили адрес, то нужный блок в свопе
	// выполняем страничное прерывание
	if (!raddr)
	{
		int __errno = swap(getLeastActivePageInMemory(), &pageTable[indexPage]);
		raddr = convertVirtualtoPhysicalAddress(ptr, &indexPage, &offsetBlock);
	}
	//___print_memory();
	//readBlock = findBlock(indexPage, offsetBlock);
	//if (szBuffer <= readBlock -> szBlock)
	{
		//vaBuffer = (VA) pBuffer;
		for (j = 0; j < szBuffer; j++)
			printf("%c", raddr[j]);
			//vaBuffer[j] = raddr[j];
		//pBuffer = (void*) vaBuffer;
		//return SUCCESSFUL_CODE;
	}
}