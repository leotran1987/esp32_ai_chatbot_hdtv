#include <Arduino.h>
#include <vector>
#include "fabgl.h"
#include "yahoo_data.h"

fabgl::VGA16Controller DisplayController;
fabgl::Canvas cv(&DisplayController);

// =========================
// UART CONFIG
// =========================
#define RX2 16
#define TX2 17

// =========================
// CHAT CONFIG
// =========================
#define CHAT_X 13
#define CHAT_Y 130
#define CHAT_W 616
#define CHAT_H 198

#define SB_W 6
#define SB_X (CHAT_X + CHAT_W - SB_W)

// =========================
// FONT CONFIG (IMPORTANT FIX)
// =========================
const int LINE_H = 26;     // 🔥 FIX LINE SPACING FOR FONT_10x20
const int CHAR_W = 10;

int scrollOffset = 0;

// =========================
// INPUT CONFIG
// =========================
#define INPUT_X 13
#define INPUT_Y 379
#define INPUT_W 540
#define INPUT_H 63


String currentStatus = "LISTENING"; // Default status
#define STATUS_X 30
#define STATUS_Y 452

// =========================
// DATA
// =========================
struct ChatLine
{
    String text;
    uint8_t type;   // 0=normal, 1=AI, 2=ME
};

std::vector<ChatLine> messages;



// =========================
// AI STREAMING
// =========================
String aiFullText = "";
String aiCurrentText = "";

std::vector<String> aiWords;

bool aiTyping = false;

int aiWordIndex = 0;

unsigned long lastAIWord = 0;

const int AI_WORD_DELAY = 120;   // ms




String inputText = "";
String mePreviewText = "";
bool cursorVisible = true;
unsigned long lastBlink = 0;



void drawStatus()
{
    // Clear old status area (fill with background color)
    cv.setBrushColor(fabgl::Color::BrightWhite); // Adjust to match the corner background color
    cv.fillRectangle(STATUS_X, STATUS_Y, STATUS_X + 400, STATUS_Y + 20);
    
    cv.setPenColor(fabgl::Color::BrightBlack);
    cv.drawText(STATUS_X, STATUS_Y, ("[Status] " + currentStatus).c_str());
}


// =========================
// BACKGROUND
// =========================
void drawBackground()
{
    cv.drawBitmap(0, 0, &yahoo);
}

// =========================
// CLEAR CHAT AREA
// =========================
void clearChatArea()
{
    cv.setBrushColor(fabgl::Color::White);
    cv.fillRectangle(CHAT_X, CHAT_Y, CHAT_X + CHAT_W, CHAT_Y + CHAT_H);
}


// =========================
// CLEAR INPUT AREA
// =========================
void clearInputArea()
{
    cv.setBrushColor(fabgl::Color::White);
    cv.fillRectangle(INPUT_X, INPUT_Y, INPUT_X + INPUT_W, INPUT_Y + INPUT_H);
}




// =========================
// NORMALIZE TEXT (SAFE ASCII)
// =========================
String normalizeEnglishText(const String &s)
{
    String out = "";

    for (int i = 0; i < s.length(); i++)
    {
        unsigned char c = s[i];

        if (c >= 32 && c <= 126)
        {
            out += (char)c;
            continue;
        }

        if ((c & 0xF0) == 0xE0)
        {
            out += '-';
            i += 2;
            continue;
        }

        if ((c & 0xE0) == 0xC0)
        {
            if (c == 0xC2 && i + 1 < s.length() && (unsigned char)s[i + 1] == 0xA0)
                out += ' ';
            i += 1;
            continue;
        }

        out += ' ';
    }

    return out;
}

// =========================
// WRAP TEXT
// =========================
std::vector<String> wrapText(const String &text, int maxWidthPx)
{
    std::vector<String> lines;

    String line = "";
    String word = "";

    int lineWidth = 0;
    int wordWidth = 0;

    for (int i = 0; i < text.length(); i++)
    {
        char c = text[i];

        // =========================
        // Found space -> flush word
        // =========================
        if (c == ' ')
        {
            // If adding word exceeds line width -> new line
            if (lineWidth + wordWidth > maxWidthPx)
            {
                lines.push_back(line);
                line = word;
                lineWidth = wordWidth;
            }
            else
            {
                if (line.length() > 0)
                {
                    line += " ";
                    lineWidth += CHAR_W;
                }

                line += word;
                lineWidth += wordWidth;
            }

            word = "";
            wordWidth = 0;
        }
        else
        {
            word += c;
            wordWidth += CHAR_W;
        }
    }

    // =========================
    // Flush last word
    // =========================
    if (word.length() > 0)
    {
        if (lineWidth + wordWidth > maxWidthPx)
        {
            lines.push_back(line);
            lines.push_back(word);
        }
        else
        {
            if (line.length() > 0)
                line += " ";

            line += word;
            lines.push_back(line);
        }
    }
    else if (line.length() > 0)
    {
        lines.push_back(line);
    }

    return lines;
}


// =========================
// INPUT
// =========================
void redrawInput()
{
    clearInputArea();  
  
    cv.setBrushColor(fabgl::Color::BrightWhite);

    cv.fillRectangle(
        INPUT_X,
        INPUT_Y,
        INPUT_X + INPUT_W,
        INPUT_Y + INPUT_H
    );

    int maxWidth = INPUT_W - 12;

    std::vector<String> lines =
        wrapText(mePreviewText, maxWidth);

    int maxLines = INPUT_H / LINE_H;

    int startLine = 0;   // Always show from the beginning of the message

    int y = INPUT_Y + 12;

    cv.setPenColor(
        fabgl::Color::BrightRed
    );

    // ONLY DRAW MAXIMUM OF maxLines
    int endLine = min(
        (int)lines.size(),
        startLine + maxLines
    );

    bool truncated =
    lines.size() > maxLines;

    for (int i = startLine; i < endLine; i++)
    {
        String lineText = lines[i];

        // Last line and has more content
        if (truncated && i == endLine - 1)
        {
            if (lineText.length() > 3)
            {
                lineText.remove(lineText.length() - 3);
                lineText += "...";
            }
        }

        cv.drawText(
            INPUT_X + 6,
            y,
            lineText.c_str()
        );

        y += LINE_H;
    }
}

// =========================
// SCROLLBAR
// =========================
void drawScrollbar()
{
    int total = messages.size();
    int maxVisible = CHAT_H / LINE_H;

    if (total <= maxVisible) return;

    int trackH = CHAT_H;

    cv.setBrushColor(fabgl::Color::Black);
    cv.fillRectangle(SB_X, CHAT_Y, SB_X + SB_W, CHAT_Y + trackH);

    int maxScroll = max(0, total - maxVisible);

    float ratio = (float)maxVisible / total;
    int thumbH = max(20, (int)(trackH * ratio));

    float pos = (float)scrollOffset / maxScroll;
    int thumbY = CHAT_Y + (int)(pos * (trackH - thumbH));

    cv.setBrushColor(fabgl::Color::BrightWhite);
    cv.fillRectangle(SB_X, thumbY, SB_X + SB_W, thumbY + thumbH);
}

// =========================
// REDRAW CHAT (FIXED)
// =========================
void redrawChat()
{
    clearChatArea();

    cv.setBrushColor(fabgl::Color::BrightWhite);

    cv.fillRectangle(
        CHAT_X,
        CHAT_Y,
        CHAT_X + CHAT_W,
        CHAT_Y + CHAT_H
    );

    // =========================
    // BUILD TEMP DISPLAY LIST
    // =========================

    std::vector<ChatLine> displayLines;

    // Previous history
    for (auto &m : messages)
        displayLines.push_back(m);

    // AI streaming
    if (aiTyping && aiCurrentText.length())
    {
        int maxWidth = CHAT_W - SB_W - 10;

        std::vector<String> wrapped =
            wrapText(aiCurrentText, maxWidth);

        for (auto &line : wrapped)
        {
            ChatLine item;
            item.text = line;
            item.type = 1;

            displayLines.push_back(item);
        }
    }

    // =========================
    // AUTO SCROLL
    // =========================

    int total = displayLines.size();

    int maxVisible = CHAT_H / LINE_H;

    if (total > maxVisible)
        scrollOffset = total - maxVisible;
    else
        scrollOffset = 0;

    // =========================
    // DRAW
    // =========================

    int y = CHAT_Y + 8;

    for (int i = scrollOffset; i < total; i++)
    {
        if (y + LINE_H > CHAT_Y + CHAT_H)
            break;

        switch (displayLines[i].type)
        {
            case 1:
                cv.setPenColor(fabgl::Color::BrightBlue);
                break;

            case 2:
                cv.setPenColor(fabgl::Color::BrightRed);
                break;

            default:
                cv.setPenColor(fabgl::Color::Black);
                break;
        }

        cv.drawText(
            CHAT_X + 2,
            y,
            displayLines[i].text.c_str()
        );

        y += LINE_H;
    }

    // =========================
    // SCROLLBAR
    // =========================

    if (total > maxVisible)
    {
        int trackH = CHAT_H;

        cv.setBrushColor(fabgl::Color::Black);

        cv.fillRectangle(
            SB_X,
            CHAT_Y,
            SB_X + SB_W,
            CHAT_Y + trackH
        );

        int maxScroll = total - maxVisible;

        float ratio =
            (float)maxVisible / total;

        int thumbH =
            max(20, (int)(trackH * ratio));

        float pos =
            (float)scrollOffset / maxScroll;

        int thumbY =
            CHAT_Y +
            (int)(pos * (trackH - thumbH));

        cv.setBrushColor(
            fabgl::Color::BrightWhite
        );

        cv.fillRectangle(
            SB_X,
            thumbY,
            SB_X + SB_W,
            thumbY + thumbH
        );
    }
}

// =========================
// ADD MESSAGE
// =========================
void addMessage(const String &msg, uint8_t type)
{
    int maxWidth = CHAT_W - SB_W - 10;

    std::vector<String> lines = wrapText(msg, maxWidth);

    for (auto &line : lines)
    {
        ChatLine item;
        item.text = line;
        item.type = type;

        messages.push_back(item);
    }

    redrawChat();
}


void startAIStreaming(const String &text)
{
    aiWords.clear();

    String word = "";

    for (int i = 0; i < text.length(); i++)
    {
        char c = text[i];

        if (c == ' ')
        {
            if (word.length())
            {
                aiWords.push_back(word);
                word = "";
            }
        }
        else
        {
            word += c;
        }
    }

    if (word.length())
        aiWords.push_back(word);

    aiFullText = text;
    aiCurrentText = "";

    aiWordIndex = 0;
    aiTyping = true;

    lastAIWord = millis();
}

void updateAIStreaming()
{
    if (!aiTyping)
        return;

    if (millis() - lastAIWord < AI_WORD_DELAY)
        return;

    lastAIWord = millis();

    if (aiWordIndex < aiWords.size())
    {
        if (aiCurrentText.length())
            aiCurrentText += " ";

        aiCurrentText += aiWords[aiWordIndex];

        aiWordIndex++;

        redrawChat();
    }
    else
    {
        aiTyping = false;

        String finalText = aiCurrentText;

        aiCurrentText = "";
        aiFullText = "";

        aiWords.clear();

        addMessage(finalText, 1);
    }
}


// =========================
// UART2
// =========================
void handleUART2()
{
    if (!Serial2.available()) return;

    String line = Serial2.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) return;

    line = normalizeEnglishText(line);

    // --- STATUS HANDLING ---
    if (line.startsWith("[STATE]"))
    {
        currentStatus = line.substring(7); // Get part after "[STATE] "
        currentStatus.trim();
        drawStatus(); // Draw new status to screen
        return;
    }
    // ------------------------

    if (line.startsWith("<<"))
    {
        String msg = line.substring(2);

        // Skip if contains "% "
        if (msg.indexOf("% ") != -1)
            return;

        startAIStreaming("AI: " + msg);
    }
    else if (line.startsWith(">>"))
    {
        String msg = line.substring(2);

        mePreviewText = msg;

        addMessage(
            "Me: " + msg,
            2
        );

        redrawInput();
    }
    else
    {
        addMessage(line, 0);
    }

}


// =========================
// SETUP
// =========================
void setup()
{
    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, RX2, TX2);

    DisplayController.begin();
    DisplayController.setResolution(VGA_640x480_60Hz);

    cv.clear();
    cv.selectFont(&fabgl::FONT_10x20);

    drawBackground();

    drawStatus();

    redrawInput();
    redrawChat();

    Serial.println("SYSTEM READY");
}

// =========================
// LOOP
// =========================
void loop()
{
    handleUART2();

    updateAIStreaming();

    if (millis() - lastBlink > 500)
    {
        cursorVisible = !cursorVisible;
        lastBlink = millis();
        redrawInput();
    }
}