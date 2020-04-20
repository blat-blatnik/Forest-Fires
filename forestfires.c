/*
	*===================================*
	|           forestfires.c           |
	*===================================*
	|         by Blat Blatnik           |
	|-----------------------------------|
	|                                   |
	| A very rough simulation of forest |
	| fires done in a console window.   |
	| It's mostly made as a proof of    |
	| concept for rendering to console  |
	| windows and handling mouse and    |
	| keyboard input in a console app.  |
	|                                   |
	|--------- how to compile ----------|
	|                                   |
	| NOTE that this program obviously  |
	| relies very heavily on WIN32      |
	| console functions so it will only |
	| work on windows. Other then that  |
	| the only thing you need is a C99  |
	| compiler.                         |
	|                                   |
	|------------ controls -------------|
	|                                   |
	| ESC             close the program |
	| left-click            place trees |
	| right-click          create fires |
	|                                   |
	|----------- "licence" -------------|
	|                                   |
	| Released into the public domain.  |
	| You can do whatever you want with |
	| this code - you don't even have   |
	| to mention where you got it if    |
	| you really don't want to.         |
	| Although I would appreciate it :) |
	|                                   |
	*===================================*
*/

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const double saplingProb    = 0.000003;
const double treeSpreadProb = 0.005;
const double fireProb       = 0.000025;

enum {
	Width  = 140,
	Height = 50,
};

typedef enum Color {
	Black      = 0,
	DarkRed    = FOREGROUND_RED,
	DarkGreen  = FOREGROUND_GREEN,
	DarkBlue   = FOREGROUND_BLUE,
	DarkYellow = DarkRed     | DarkGreen,
	DarkPurple = DarkRed     | DarkBlue,
	DarkCyan   = DarkBlue    | DarkGreen,
	Gray       = DarkRed     | DarkBlue | DarkGreen,
	Red        = DarkRed     | FOREGROUND_INTENSITY,
	Green      = DarkGreen   | FOREGROUND_INTENSITY,
	Blue       = DarkBlue    | FOREGROUND_INTENSITY,
	Yellow     = DarkYellow  | FOREGROUND_INTENSITY,
	Purple     = DarkPurple  | FOREGROUND_INTENSITY,
	Cyan       = DarkCyan    | FOREGROUND_INTENSITY,
	White      = Gray        | FOREGROUND_INTENSITY,
} Color;

typedef enum Cell {
	Empty,
	Sapling1,
	Sapling2,
	Sapling3,
	Sapling4,
	Sapling5,
	Tree,
	SizzlingTree,
	HotTree,
	BurningTree1,
	BurningTree2,
	BurningTree3,
	BurningTree4,
	BurningTree5,
	BurningTree6,
	BurningTree7,
	BurningTree8,
	BurningTree9,
	HotBurntTree,
	BurntTree,
	CollapsedTree,
} Cell;

Cell newCells[Height][Width];
Cell oldCells[Height][Width];
CHAR_INFO consoleChars[Height][Width];
CHAR_INFO cellChars[CollapsedTree + 1];

unsigned int rngState; /* xorshift RNG state */
HANDLE consoleBuffer;
HANDLE inputBuffer;
int mouseX = -1, mouseY = -1;
int leftButtonPressed, rightButtonPressed;

unsigned int randu() {
	/* xorshift: https://en.wikipedia.org/wiki/Xorshift#Example_implementation */
	unsigned int x = rngState;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rngState = x;
	return x;
}

unsigned short makeTextAttrib(Color foreground, Color background) {
	return (unsigned short)(foreground | (background << 4));
}

void initConsole() {
	consoleBuffer = GetStdHandle(STD_OUTPUT_HANDLE);
	inputBuffer   = GetStdHandle(STD_INPUT_HANDLE);

	COORD cursorPos;
	cursorPos.X = 0;
	cursorPos.Y = 0;

	/* Set up the console screen-buffer. */
	SetConsoleTitleA("Forest Fires");
	SetConsoleCursorPosition(consoleBuffer, cursorPos);

	/* Now we need to resize the console and thats apparently supper finicky here on Windows ...
	   The main problem is that in order to resize the console window, the console buffer needs
	   to be at least as big as the new window - BUT in order to resize the console buffer, the
	   window needs to be at least as big as the console buffer.... UGHHHH....

	   The solution seems to be to just first set the window to be really small, then resize the
	   console buffer, and THEN resize the window to fit the buffer exactly.. */

	SMALL_RECT consoleSize;
	consoleSize.Top    = 0;
	consoleSize.Left   = 0;
	consoleSize.Bottom = 1;
	consoleSize.Right  = 1;

	SetConsoleWindowInfo(consoleBuffer, TRUE, &consoleSize);

	COORD bufferSize;
	bufferSize.X = Width;
	bufferSize.Y = Height;

	consoleSize.Bottom = Height - 1;
	consoleSize.Right  = Width  - 1;

	BOOL success;

	success = SetConsoleScreenBufferSize(consoleBuffer, bufferSize);
	success = SetConsoleWindowInfo(consoleBuffer, TRUE, &consoleSize);

	/* Hide the console cursor. */
	CONSOLE_CURSOR_INFO cursorInfo;
	GetConsoleCursorInfo(consoleBuffer, &cursorInfo);
	cursorInfo.bVisible = FALSE;
	SetConsoleCursorInfo(consoleBuffer, &cursorInfo);

	/* Enable mouse input - ENABLE_EXTENDED_FLAGS will turn off "quick edit mode"
	   which for some reason normally prevents all mouse input from being generated.. */
	SetConsoleMode(inputBuffer, ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT);
}

void updateConsole(void) {
	COORD topLeft;
	topLeft.X = 0;
	topLeft.Y = 0;
	COORD charBufferSize;
	charBufferSize.X = Width;
	charBufferSize.Y = Height;
	SMALL_RECT writeRegion;
	writeRegion.Top    = 0;
	writeRegion.Left   = 0;
	writeRegion.Bottom = Height;
	writeRegion.Right  = Width;
	WriteConsoleOutputA(consoleBuffer, &consoleChars[0][0], charBufferSize, topLeft, &writeRegion);
}

void initSimulation() {
	cellChars[Empty].Char.AsciiChar         = ' ';
	cellChars[Sapling1].Char.AsciiChar      = ',';
	cellChars[Sapling2].Char.AsciiChar      = ',';
	cellChars[Sapling3].Char.AsciiChar      = ',';
	cellChars[Sapling4].Char.AsciiChar      = ',';
	cellChars[Sapling5].Char.AsciiChar      = ',';
	cellChars[Tree].Char.AsciiChar          = 'T';
	cellChars[SizzlingTree].Char.AsciiChar  = 'T';
	cellChars[HotTree].Char.AsciiChar       = 'T';
	cellChars[BurningTree1].Char.AsciiChar  = 'T';
	cellChars[BurningTree2].Char.AsciiChar  = 'T';
	cellChars[BurningTree3].Char.AsciiChar  = 'T';
	cellChars[BurningTree4].Char.AsciiChar  = 'T';
	cellChars[BurningTree5].Char.AsciiChar  = 'T';
	cellChars[BurningTree6].Char.AsciiChar  = 'T';
	cellChars[BurningTree7].Char.AsciiChar  = 'T';
	cellChars[BurningTree8].Char.AsciiChar  = 'T';
	cellChars[BurningTree9].Char.AsciiChar  = 'T';
	cellChars[HotBurntTree].Char.AsciiChar  = 'T';
	cellChars[BurntTree].Char.AsciiChar     = 'T';
	cellChars[CollapsedTree].Char.AsciiChar = ',';
	cellChars[Empty].Attributes         = makeTextAttrib(Black, Black);
	cellChars[Sapling1].Attributes      = makeTextAttrib(DarkGreen, Black);
	cellChars[Sapling2].Attributes      = makeTextAttrib(DarkGreen, Black);
	cellChars[Sapling3].Attributes      = makeTextAttrib(DarkGreen, Black);
	cellChars[Sapling4].Attributes      = makeTextAttrib(Green, Black);
	cellChars[Sapling5].Attributes      = makeTextAttrib(Green, Black);
	cellChars[Tree].Attributes          = makeTextAttrib(Green, Black);
	cellChars[SizzlingTree].Attributes  = makeTextAttrib(DarkRed, Black);
	cellChars[HotTree].Attributes       = makeTextAttrib(Red, DarkRed);
	cellChars[BurningTree1].Attributes  = makeTextAttrib(DarkRed, DarkRed);
	cellChars[BurningTree2].Attributes  = makeTextAttrib(DarkRed, DarkRed);
	cellChars[BurningTree3].Attributes  = makeTextAttrib(DarkRed, Red);
	cellChars[BurningTree4].Attributes  = makeTextAttrib(Red, Red);
	cellChars[BurningTree5].Attributes  = makeTextAttrib(Red, Red);
	cellChars[BurningTree6].Attributes  = makeTextAttrib(Red, Red);
	cellChars[BurningTree7].Attributes  = makeTextAttrib(DarkRed, Red);
	cellChars[BurningTree8].Attributes  = makeTextAttrib(DarkRed, DarkRed);
	cellChars[BurningTree9].Attributes  = makeTextAttrib(DarkRed, DarkRed);
	cellChars[HotBurntTree].Attributes  = makeTextAttrib(DarkRed, Black);
	cellChars[BurntTree].Attributes     = makeTextAttrib(Gray, Black);
	cellChars[CollapsedTree].Attributes = makeTextAttrib(Gray, Black);
}

void updateCell(int y, int x) {
	int treeNeighbors = 0;
	int burningNeighbors = 0;

	Cell leftNeighbor   = x > 0      ? oldCells[y][x - 1] : Empty;
	Cell rightNeighbor  = x < Width  ? oldCells[y][x + 1] : Empty;
	Cell topNeighbor    = y > 0      ? oldCells[y - 1][x] : Empty;
	Cell bottomMeighbor = y < Height ? oldCells[y + 1][x] : Empty;
	treeNeighbors += leftNeighbor   == Tree;
	treeNeighbors += rightNeighbor  == Tree;
	treeNeighbors += topNeighbor    == Tree;
	treeNeighbors += bottomMeighbor == Tree;
	burningNeighbors += (leftNeighbor   >= BurningTree1 && leftNeighbor   <= BurningTree9);
	burningNeighbors += (rightNeighbor  >= BurningTree1 && rightNeighbor  <= BurningTree9);
	burningNeighbors += (topNeighbor    >= BurningTree1 && topNeighbor    <= BurningTree9);
	burningNeighbors += (bottomMeighbor >= BurningTree1 && bottomMeighbor <= BurningTree9);

	Cell newCell = Empty;
	Cell cell = oldCells[y][x];
	double r = randu() / (double)0xFFFFFFFF;
	
	if (cell == Empty) {
		double prob = treeNeighbors > 0 ? treeSpreadProb : saplingProb;
		/* Create a sampling either randomly - or because left mouse button is pressed. */
		if (r < prob || (leftButtonPressed && x == mouseX && y == mouseY))
			newCell = Sapling1;
	} 
	else if (cell == Tree) {
		/* Burn the tree either randomly - or because right mouse button is pressed. */
		if (burningNeighbors > 0 || r < fireProb || (rightButtonPressed && x == mouseX && y == mouseY))
			newCell = SizzlingTree;
		else
			newCell = Tree;
	} 
	else if (cell + 1 <= CollapsedTree) {
		newCell = cell + 1;
	}

	newCells[y][x] = newCell;
	consoleChars[y][x] = cellChars[newCell];
	if (x == mouseX && y == mouseY) {
		/* Highlight the cell with the mouse cursor. */
		consoleChars[y][x].Attributes &= ~(0xF0);
		consoleChars[y][x].Attributes |= (DarkGreen << 4);
	}
}

void simulateTimestep() {
	for (int y = 0; y < Height; ++y)
		for (int x = 0; x < Width; ++x)
			updateCell(y, x);
	memcpy(&oldCells[0][0], &newCells[0][0], sizeof(oldCells));
}

int main(void) {

	rngState = (unsigned int)time(NULL);
	rngState |= 1; /* xorshift needs the seed to be != 0 */

	initConsole();
	initSimulation();

	for (;;) {

		/* We need to poll because we don't get specific mouse released events. */
		if (leftButtonPressed)
			leftButtonPressed  = GetAsyncKeyState(VK_LBUTTON) >> 15;
		if (rightButtonPressed)
			rightButtonPressed = GetAsyncKeyState(VK_RBUTTON) >> 15;

		simulateTimestep();

		DWORD numEvents;
		GetNumberOfConsoleInputEvents(inputBuffer, &numEvents);
		while (numEvents > 0) {
			
			/* Read a single input events at a time. */
			INPUT_RECORD input;
			ReadConsoleInputA(inputBuffer, &input, 1, &numEvents);
			
			if (input.EventType == KEY_EVENT && input.Event.KeyEvent.bKeyDown) {
				if (input.Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE)
					return 0; /* close the application */
			} else if (input.EventType == MOUSE_EVENT) {

				int x = input.Event.MouseEvent.dwMousePosition.X;
				int y = input.Event.MouseEvent.dwMousePosition.Y;

				if (input.Event.MouseEvent.dwEventFlags == 0) { /* mouse button press */
					if (input.Event.MouseEvent.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) {
						leftButtonPressed = 1;
						if (newCells[y][x] == Empty) {
							oldCells[y][x] = Sapling1;
							consoleChars[y][x] = cellChars[Sapling1];
						}
					}

					if (input.Event.MouseEvent.dwButtonState & RIGHTMOST_BUTTON_PRESSED) {
						rightButtonPressed = 1;
						if (newCells[y][x] == Tree) {
							oldCells[y][x] = SizzlingTree;
							consoleChars[y][x] = cellChars[SizzlingTree];
						}
					}
				} else if (input.Event.MouseEvent.dwEventFlags == MOUSE_MOVED) {
					mouseX = x;
					mouseY = y;
				}
			}

			GetNumberOfConsoleInputEvents(inputBuffer, &numEvents);
		}
		

		updateConsole();
		Sleep(16); /* no vsync - but this does the trick */
	}

	return 0;
}