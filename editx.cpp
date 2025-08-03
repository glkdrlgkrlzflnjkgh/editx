#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <limits>
#include <cctype>
#include <cstdlib> // add the standard library
#undef min
#undef max
#undef clamp
#ifdef _MSC_VER
    #pragma warning(disable : 4996) // Disable deprecation warnings for fopen, etc.
#endif

std::vector<std::string> buffer;
int cursorX = 0, cursorY = 0;
int viewOffsetY = 0;
int viewHeight = 120;
int consoleWidth = 80;

bool running = true;
bool inPrompt = false;
std::string filename = "editx.txt";
std::string promptText = "";
std::string promptInput = "";

enum Mode { EDIT, PROMPT_WRITE, PROMPT_LOAD, PROMPT_HELP, ERRORED };
Mode currentMode = EDIT;

HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

bool isRegularFile(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

void setCursor(int x, int y) {
    COORD pos = { static_cast<SHORT>(x), static_cast<SHORT>(y) };
    SetConsoleCursorPosition(hOut, pos);
}

void clearScreen() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    DWORD cells = csbi.dwSize.X * csbi.dwSize.Y;
    DWORD written;
    COORD origin = { 0, 0 };
    FillConsoleOutputCharacterA(hOut, ' ', cells, origin, &written);
    FillConsoleOutputAttribute(hOut, csbi.wAttributes, cells, origin, &written);
    setCursor(0, 0);
}

void updateViewport() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    int windowWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int windowHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    csbi.wAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

    int previousViewHeight = viewHeight;
    viewHeight = windowHeight - 3;
    consoleWidth = windowWidth;

    COORD newSize = { static_cast<SHORT>(windowWidth), static_cast<SHORT>(windowHeight) };
    SMALL_RECT newWin = { 0, 0, static_cast<SHORT>(windowWidth - 1), static_cast<SHORT>(windowHeight - 1) };
    SetConsoleScreenBufferSize(hOut, newSize);
    SetConsoleWindowInfo(hOut, TRUE, &newWin);

    int maxOffset = std::max(0, static_cast<int>(buffer.size()) - viewHeight);
    if (cursorY >= viewOffsetY + viewHeight || cursorY < viewOffsetY) {
        viewOffsetY = std::clamp(cursorY - viewHeight / 2, 0, maxOffset);
    }
    else if (viewHeight > previousViewHeight) {
        viewOffsetY = std::clamp(cursorY - viewHeight, 0, maxOffset);
    }
}

void render() {
    updateViewport();
    clearScreen();
    std::cout << "\x1b[3J";

    int totalLines = static_cast<int>(buffer.size());
    int linesToRender = std::min(viewHeight, totalLines - viewOffsetY);

    for (int i = 0; i < linesToRender; ++i) {
        int index = viewOffsetY + i;
        if (index < static_cast<int>(buffer.size())) {
            const auto& line = buffer[index];
            if (line.size() < consoleWidth - 2)
                std::cout << line << std::endl;
            else
                std::cout << line.substr(0, consoleWidth - 3) << "...\n";
        }
    }

    for (int i = linesToRender; i < viewHeight; ++i)
        std::cout << std::string(consoleWidth, ' ') << "\n";
    if (filename.length() > consoleWidth - 30) {
        filename = filename.substr(0, consoleWidth - 30) + "...";
	}   
    std::cout << "File: " << filename
        << " | Line: " << (cursorY + 1)
        << " Col: " << (cursorX + 1)
        << " | Ctrl+S=Save Ctrl+O=Write Ctrl+L=Load Ctrl+Q=Quit Cntrl+H=Credits\n"
        << "Use arrow keys to navigate, Enter to insert new line, Backspace to delete\n";

    if (inPrompt) std::cout << promptText << promptInput;

    int y = inPrompt ? viewHeight + 1 : cursorY - viewOffsetY;
    setCursor(cursorX, y);
}

void insertChar(char c) {
    if (cursorY >= static_cast<int>(buffer.size())) buffer.push_back("");
    buffer[cursorY].insert(cursorX, 1, c);
    cursorX++;
}

void saveToFile(const std::string& fname) {
    if (fname.find("System32")) {
       
        buffer.clear();
        buffer.push_back("Error: Cannot save to System32 or similar directories.");
        currentMode = ERRORED;
        clearScreen();
        buffer.push_back("");
        cursorX = cursorY = viewOffsetY = 0;
		filename = "Error";
		return; // Prevent saving to System32 or similar directories
    }
    std::ofstream out(fname);
    for (const auto& line : buffer) out << line << "\n";
    filename = fname;
	cursorX = cursorY = viewOffsetY = 0;
	clearScreen();
}

void loadFile(const std::string& fname) {
    try {
        if (!isRegularFile(fname)) {
			throw std::runtime_error("File does not exist or is not a regular file");
            
		}
        std::ifstream in(fname);
        buffer.clear();
        std::string line;
        while (std::getline(in, line)) buffer.push_back(line);
        if (buffer.empty()) buffer.push_back("");
        filename = fname;
        cursorX = cursorY = viewOffsetY = 0;
    }
    catch (const std::exception& e) {
        
        buffer.clear();
		buffer.push_back("Error loading file: " + std::string(e.what())+ "." + " Sorry about that.");
		filename = "Error";
        currentMode = ERRORED;
		clearScreen();
        buffer.push_back("");
        cursorX = cursorY = viewOffsetY = 0;
    }
}

void startPrompt(const std::string& text, Mode mode) {
    inPrompt = true;
    promptText = text;
    promptInput.clear();
    currentMode = mode;
}

void exitPrompt(bool confirmed) {
    if (confirmed) {
        if (currentMode == PROMPT_WRITE) saveToFile(promptInput);
        else if (currentMode == PROMPT_LOAD) loadFile(promptInput);
    }
    inPrompt = false;
    promptText.clear();
    promptInput.clear();
    currentMode = EDIT;
}

void handleEditingKey(KEY_EVENT_RECORD& key) {
    if (!key.bKeyDown) return;
    WORD vk = key.wVirtualKeyCode;
    char ch = key.uChar.AsciiChar;
    DWORD ctrl = key.dwControlKeyState;

    if ((vk == 'Q') && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) running = false;
    else if ((vk == 'S') && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) saveToFile(filename);
    else if ((vk == 'O') && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) startPrompt("File Name to Write: ", PROMPT_WRITE);
    else if ((vk == 'L') && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) startPrompt("File Name to Load: ", PROMPT_LOAD);
    else if ((vk == 'H') && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) startPrompt("CREDITS: made by callum Nicoll, all rights reserved copyright callum nicoll 2025", PROMPT_HELP);
    else if (vk == VK_RETURN) {
        buffer.insert(buffer.begin() + cursorY + 1, "");
        cursorY++;
        cursorX = 0;
    }
    else if (vk == VK_BACK && cursorX > 0) {
        buffer[cursorY].erase(cursorX - 1, 1);
        cursorX--;
    }
    else if (vk == VK_LEFT && cursorX > 0) cursorX--;
    else if (vk == VK_RIGHT && cursorX < static_cast<int>(buffer[cursorY].size())) cursorX++;
    else if (vk == VK_UP && cursorY > 0) {
        cursorY--;
        cursorX = std::min(cursorX, static_cast<int>(buffer[cursorY].size()));
    }
    else if (vk == VK_DOWN && cursorY < static_cast<int>(buffer.size()) - 1) {
        cursorY++;
        cursorX = std::min(cursorX, static_cast<int>(buffer[cursorY].size()));
    }
    else if (ch >= 32 && ch <= 126) insertChar(ch);
}

void handlePromptKey(KEY_EVENT_RECORD& key, Mode mode) {
    if (mode == PROMPT_HELP) {
        if (key.wVirtualKeyCode == VK_RETURN) {
            inPrompt = false;
            promptText.clear();
            promptInput.clear();
            currentMode = EDIT;
        }
        return;
    }

    if (!key.bKeyDown) return;
    char ch = key.uChar.AsciiChar;
    WORD vk = key.wVirtualKeyCode;

    if (vk == VK_RETURN) exitPrompt(true);
    else if (vk == VK_ESCAPE) exitPrompt(false);
    else if (vk == VK_BACK && !promptInput.empty()) promptInput.pop_back();
    else if (ch >= 32 && ch <= 126) promptInput += ch;
}

int main(int argc, char* argv[]) {
    DWORD mode = 0;
    GetConsoleMode(hIn, &mode);
    mode |= ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS;
    mode &= ~ENABLE_QUICK_EDIT_MODE;
    SetConsoleMode(hIn, mode);

    SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_PROCESSED_INPUT);
    updateViewport();

    if (argc > 1) loadFile(argv[1]);
    else buffer.push_back("");

    while (running) {
        render();
        INPUT_RECORD input[128];
        DWORD count;
        ReadConsoleInput(hIn, input, 128, &count);
        if (currentMode == ERRORED) {
            render();
            continue;
		}
        for (DWORD i = 0; i < count; ++i) {
            if (input[i].EventType == KEY_EVENT) {
                if (inPrompt)
                    handlePromptKey(input[i].Event.KeyEvent, currentMode);
                else
                    handleEditingKey(input[i].Event.KeyEvent);
            }
            else if (input[i].EventType == MOUSE_EVENT) {
                MOUSE_EVENT_RECORD& mouse = input[i].Event.MouseEvent;
                if (mouse.dwEventFlags == MOUSE_WHEELED) {
                    short delta = static_cast<short>(HIWORD(mouse.dwButtonState));
                    if (delta > 0)
                        viewOffsetY = std::max(0, viewOffsetY - 3);
                    else if (delta < 0)
                        viewOffsetY = std::min(
                            std::max(0, static_cast<int>(buffer.size()) - viewHeight),
                            viewOffsetY + 3);
                }
            }
        }
    }
}