//Tomaszkowa zmiana numer 1
#pragma warning(disable : 4996)     //dla fopen.

#include <fstream>
#include <iostream>
#include <string>
#include <algorithm>    // For std::remove()

#include <stdio.h>
#include <stdlib.h>

#include <filesystem>
namespace fs = std::filesystem;
using std::filesystem::current_path;


#include <vector>
#include <Windows.h>

using namespace std;

string ver = "1.01";        //wersja kompilowane
#define DEBUG 0             //wyswietlanie informacji pomocniczych

vector<string> listOfFiles;

FILE inputFile;                         // plik wejsciowy do obrobki
FILE *outputFile;                       // plik wyjsciowy;
#define FILE_MAX_INPUT_SIZE 1073741824  // maksymalny romiar pliku w bajtach 
#define FILE_OK 0                       // nie ma problemow z plikem
#define FILE_NOT_EXIST 1                // plik nie istnieje
#define FILE_TO_LARGE 2                 // plik wiekszy niz FILE_MAX_INPUT_SIZE
#define FILE_READ_ERROR 3               // problem odczytu

#define FILE_TYPE_IS_INPUT 0            // plik jako parametr to plik do parsowania
#define FILE_TYPE_IS_OUTPUT 1           // plik jest sparsowany , trzeba wyswietlic
#define FILE_TYPE_IS_NOT_SUPORTED 2        // jakis nie obslugiwany plik


int inputError;             // tu tafia status FILE_
size_t inputSize;           // rozmiar odczytywanego pliku
int inputType;              //  z jakim plikiem mamy doczynienia 

char* inputData;            // bufor na wczytywane dane
string strData;             // dana skopiowane z bufora inputData , latweijsza obrobka
string inputFileNameBase;   // przydladowo : dialog_lb , bez rozszerzenia
string inputExt = ".f";     // obowiazkowe rozszezenie pliku wejsciowego
string outputExt = ".f2";   // rozszezenie wpliku wychodzącego.

string bitmapBlock;     // np : #10  All block needed for clearing unnessery data
string glyphBlock;	    // np : #20
string fontBlock;	    // np : #30
string stopBlock="#";   // np : #  , nie ruszac bo sie popsuje , w kodzie na pale gdzies porownywane do "#"

typedef struct {
    uint16_t bitmapOffset;
    uint8_t width;
    uint8_t height;
    uint8_t xAdvance;
    int8_t xOffset; //-127..127
    int8_t yOffset; //-127..127
} glyph_t;

typedef struct {
    uint16_t first;     // pierwszy Ascii znak
    uint16_t last;      // ostatni Ascii znak
    uint8_t yAdvance;  // przesuniecie Y dla nowego wiesza
}font_t;

typedef struct  {
    char signature[5]; // 4 na sygnature + '\n'
    uint8_t version;   // miejmy nadzieje ze pozostaniemy na 1
}mainHeader_t;

typedef struct  {
    font_t font;
    uint32_t bitmapStart; // liczone od poczatku pliku
    uint32_t bitmapSize;  // rozmiar w bajtach
    uint32_t glyphStart;  // liczone od poczatku pliku
    uint32_t glyphSize;   // rozmiar w bajtach , ilosc wierszy =  glyphSize / sizeof(glyph_t)

}versionHeader1_t;
 
mainHeader_t mainHeader = { "BDLF",1 };
versionHeader1_t verHeader1{ {1,2,3},10,20,30,40 }; //wstepnie wypelnione smieciami , potem bedzie uaktualnione

int bitmapByteCnt = 0;  // to uaktualnia ouytputWriteBitmapData();
int glyphByteCnt = 0;   // to uaktualnia outputWriteGlyphData();
font_t updatedFont;     // to uaktualnia outputWriteFontData();

//------------------------------------------------------------------------------
void createListToProcess() {
    for (const auto& entry : fs::directory_iterator("./")) {
        if (entry.file_size() < FILE_MAX_INPUT_SIZE) {
            if (entry.path().has_extension()) {
                if (entry.path().extension().string().compare(inputExt) == 0) {
                    listOfFiles.push_back(entry.path().filename().string());
                } // rozszezenie pliku ".f"
            };//ma rozszezenie
        };//nie za duzy            
    };
};
//------------------------------------------------------------------------------
bool isValidFileName(char* name, int* type) {
    string tmp = name;
    //istotna jest kolejnosc sprawdzanych rozszezen , bo input ext zalapie sie do koszyka output ext
    if (tmp.find(outputExt) != string::npos) {
        //printf("is output.\n");
        *type = FILE_TYPE_IS_OUTPUT;
        return true;
    }
    else if (tmp.find(inputExt) != string::npos) {
        //printf("is input.\n");
        *type = FILE_TYPE_IS_INPUT;
        return true;
    }
    else {
        *type = FILE_TYPE_IS_NOT_SUPORTED;
        //printf("not suported.\n");
        return false;
    };
}; 
//------------------------------------------------------------------------------
string getBaseFilename(char* name) {
    string tmp = name;
    string::size_type pos = 0;
    pos = tmp.find('.', pos);
    tmp = tmp.substr(0, pos);
    return tmp;
};
//------------------------------------------------------------------------------
char* readInputData(const char* f_name, int* err, size_t* f_size) {
    char* buffer;
    size_t length;
    FILE* f = fopen(f_name, "rb");
    size_t read_length;

    if (f) {
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (length > FILE_MAX_INPUT_SIZE) {
            *err = FILE_TO_LARGE;
            return NULL;
        };
        buffer = (char*)malloc(length + 1);

        if (length) {
            read_length = fread(buffer, 1, length, f);
            if (length != read_length) {
                free(buffer);
                *err = FILE_READ_ERROR;
                return NULL;
            };
        };
        fclose(f);
        *err = FILE_OK;
        buffer[length] = '\0';
        *f_size = length;
    }
    else {
        *err = FILE_NOT_EXIST;
        return NULL;
    };
    return buffer;
};
//------------------------------------------------------------------------------
void inputErrorInfo() {
    if (inputType == FILE_TYPE_IS_NOT_SUPORTED) {
        printf("Nie obslugiwany format pliku.");
    };
    switch (inputError) {
    case FILE_NOT_EXIST:
        printf("Plik nie istnieje.\n");
        break;
    case FILE_TO_LARGE:
        printf("Plik za duzy, maksymalny rozmiar to : %d b.\n", FILE_MAX_INPUT_SIZE);
        break;
    case FILE_READ_ERROR:
        printf("Blad odczytu pliku. \n");
        break;
    default:
        break;
    };
};
//------------------------------------------------------------------------------
void removeJunk() {
    bool toRemove = true;
    size_t start;
    size_t stop;
    string lookFor;
    #if DEBUG == 1
    printf("\n[*]Usuwam smieci.\n");
    printf("[*]Komentarze typu : // ...abc...\n");
    #endif
    while (toRemove) {
        start = strData.find("//");
        if (start !=string::npos) {
            stop = strData.find('\n', start);
            strData.erase(start, stop - start + 1);
        }
        else {
            toRemove = false;
        };
    };

    #if DEBUG == 1
    printf("[*]Komentarze : /* ...abc... */\n");
    #endif
    toRemove = true;
    while (toRemove) {
        start = strData.find("/*");
        if (start != string::npos) {
            stop = strData.find("*/", start);
            strData.erase(start, stop - start + 3);
            //printf("%d %d \n", start , stop);
        }
        else {
            toRemove = false;
        };
    };

    #if DEBUG == 1
    printf("[*]Tabulatory.\n");
    #endif
    strData.erase(remove(strData.begin(), strData.end(), '\t'), strData.end());    

    #if DEBUG == 1
    printf("[*]Karotki.\n");
    #endif
    strData.erase(remove(strData.begin(), strData.end(), '\r'), strData.end());

    #if DEBUG == 1
    printf("[*]Spacje\n");
    #endif
    strData.erase(remove(strData.begin(), strData.end(), ' '), strData.end());

    #if DEBUG == 1
    printf("[*]Zwielokrotnione znaki nowych lini.\n");
    #endif
    toRemove = true;
    while (toRemove) {
        start = strData.find("\n\n");
        if (start != string::npos) {
            strData.erase(start,1);
        }
        else {
            toRemove = false;
        };
    };
};
//------------------------------------------------------------------------------
void replaceAll(string& str, const string& from, const string& to) {
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    };
};
//------------------------------------------------------------------------------
bool createStartBlock(string startTag, string stopTag, string replaceTag, string& where) {
    string valWithTags = "";   
    size_t start;
    size_t stop;
    size_t len;
    where = replaceTag.c_str();
    start = strData.find(startTag);
    stop = strData.find(stopTag, start);
    if (start != string::npos && stop != string::npos) {
        len = stop - start + stopTag.length();
        valWithTags = strData.substr(start, len);
        #if DEBUG == 1
        printf("Oznaczenie bloku : %s \n", replaceTag.c_str());
        printf("start: %zd \nstop: %zd\n", start, stop);
        printf("Wytypowany blok : %s \n", valWithTags.c_str());
        #endif
        strData.replace(start, len, replaceTag);
        return true;
    }
    else {
        printf("Nie odnaleziono bloku startowego lub poczatkowego.\n");
        return false;
    };
};
//------------------------------------------------------------------------------
void createStopBlock(string srcTag, string dstTag, string& where) {
    where = dstTag.c_str();    
    replaceAll(strData, srcTag, dstTag);
};
//------------------------------------------------------------------------------
bool prepareStringData() {
    bool b1 , b2, b3;
    //--surowe dane
     //cout << strData;
    removeJunk();
    //--po usunieciu komentarzy, tabow, zbednych spacji, itp.
    //cout << strData;
    #if DEBUG == 1
    printf("[*]Tworze bloki.\n");
    #endif
    b1 = createStartBlock("constuint8_t", "[]PROGMEM={", "#10", bitmapBlock);
    b2 = createStartBlock("constGFXglyph", "[]PROGMEM={", "#20", glyphBlock);
    b3 = createStartBlock("constGFXfont", "(GFXglyph*)", "#30", fontBlock);
    createStopBlock("};", "#", stopBlock); //nie ruszac niech zawsze bedzie # , przekombinowalem
    #if DEBUG == 1
    printf("\n[*]Plik po wstepnym czyszczeniu: \n");
    printf("%s\n", strData.c_str());
    printf("glyph row size : %zd\n", sizeof(glyph_t));
    printf("font description size : %zd\n", sizeof(font_t));
    printf("main header size : %zd\n", sizeof(mainHeader));
    printf("version 1 header size : %zd\n", sizeof(versionHeader1_t));
    #endif
    if (b1 && b2 && b3) return true;
    else return false;
};
//------------------------------------------------------------------------------
bool outputWriteHeader(string fname) {
    string newFileName = fname;
    newFileName.append(outputExt);
    outputFile = fopen(newFileName.c_str(), "wb");
    if (outputFile) {
        fwrite(&mainHeader, sizeof(mainHeader_t), 1, outputFile);
        fwrite(&verHeader1, sizeof(versionHeader1_t), 1, outputFile);
        #if DEBUG == 1
        printf("Naglowek dodany. \n");
        #endif
        return true;
    };
    return false;
};
//------------------------------------------------------------------------------
bool outputWriteBitmapData() {
    size_t pos = 0;

    string byteStr;
    int readedByte;
    bool read = true;  

    bitmapByteCnt = 0;
    pos = strData.find_first_of(bitmapBlock);
    if (pos != string::npos) {
        pos += bitmapBlock.length() + 1; // +1 becouse : "\n"
        while (read) {
            byteStr = strData.substr(pos, 5);//wez dla przykladu: 0x10, (5 bajtow)
            readedByte = stoi(byteStr, nullptr, 16);
            bitmapByteCnt++;
            //printf("byteStr:%s val : %d\t , %d\n", byteStr.c_str(), readedByte,byteCnt);
            pos += 5; // for example : 0x10,

            if (byteStr.back() == '#') read = false;
            fputc(readedByte, outputFile);
        };
        #if DEBUG == 1
        printf("Liczba bajtow w bitmap block  : %d \n", bitmapByteCnt);
        #endif
    }
    else {
        printf("Nie odnaleziono bloku bitmapBlock : %s\n", bitmapBlock.c_str());
        return false;
    };
    return true;
};
//------------------------------------------------------------------------------
bool outputWriteGlyphData() {
    size_t pos = 0;
    bool read = true;
    string row;
    size_t start;
    size_t stop;
    glyph_t gl;
    int glyphRowCnt = 0;

    glyphByteCnt = 0;
    pos = strData.find_first_of(glyphBlock);
    if (pos != string::npos) {
        pos += glyphBlock.length() + 1; // +1 becouse : "\n"

        while (read) {
            start = strData.find('{', pos);
            stop = strData.find('}', pos + 1);
            if (start != string::npos && stop != string::npos) {
                row = strData.substr(start + 1, (stop - start - 1));
                glyphRowCnt++;              
                sscanf(row.c_str(), "%hu,%hhu,%hhu,%hhu,%hhd,%hhd", &gl.bitmapOffset, &gl.width, &gl.height, &gl.xAdvance, &gl.xOffset, &gl.yOffset);
                //printf("row: %s \n", row.c_str());
                //printf("%d) BO: %d , W:%d , H:%d , A:%d, xO: %d , yO: %d\n", glyphRowCnt, gl.bitmapOffset, gl.width, gl.height, gl.xAdvance, gl.xOffset, gl.yOffset);
                pos = stop + 2;
                fwrite(&gl, sizeof(glyph_t), 1, outputFile);
            }
            else {
                read = false;
            };
        };
        glyphByteCnt = glyphRowCnt * sizeof(glyph_t);
        #if DEBUG == 1
            printf("Liczba wierszy w glyph block  : %d \n", glyphRowCnt);
            printf("Liczba bajtow danych w glyph block : %d \n", glyphByteCnt);
        #endif
    }
    else {
        printf("Nie odnaleziono bloku glyphBlock : %s\n", glyphBlock.c_str());
    };
    return true;
};
//------------------------------------------------------------------------------
bool outputWriteFontData() {
    size_t start;
    size_t stop;
    string fontData;

    start = strData.find(fontBlock);
    stop = strData.find(stopBlock, (start + fontBlock.length()));

    if (start != string::npos && stop != string::npos) {
        fontData = strData.substr(start, (stop - start));
        start = fontData.find(',');
        fontData = fontData.substr(start + 1, fontData.length() - start);
        sscanf(fontData.c_str(), "%hx,%hx,%hhx", &updatedFont.first, &updatedFont.last, &updatedFont.yAdvance);
        #if DEBUG == 1
        printf("Dane fonta :%s \n", fontData.c_str());
        printf("Font first : %d , last : %d , yAdvance : %d \n ", updatedFont.first, updatedFont.last, updatedFont.yAdvance);
        #endif
    }
    else {
        printf("Nie odnaleziono bloku fontBlock : %s \n", fontBlock.c_str());
        return false;
    };
    return true;
};
//------------------------------------------------------------------------------
bool outputUpdateHeader() {
    fseek(outputFile, sizeof(mainHeader_t), SEEK_SET);
    verHeader1.font = updatedFont;
    verHeader1.bitmapStart = sizeof(mainHeader_t) + sizeof(versionHeader1_t);
    verHeader1.bitmapSize = bitmapByteCnt;
    verHeader1.glyphStart = verHeader1.bitmapStart + verHeader1.bitmapSize;
    verHeader1.glyphSize = glyphByteCnt;
    fwrite(&verHeader1, sizeof(versionHeader1_t), 1, outputFile);
    if (fwrite != 0) {
        printf("Plik wyjsciowy zapisany.\n");
        fclose(outputFile);
        return true;
    }else {
        printf("Blad zapisu pliku wyjsciowego. !\n");
        fclose(outputFile);
        return false;
    };
};
//------------------------------------------------------------------------------
void readTest() {
    string newFileName = inputFileNameBase.c_str();
    newFileName.append(outputExt);
    mainHeader_t mh;
    versionHeader1_t v1;

    uint8_t* bitmapBytes;
    glyph_t g;
    glyph_t* glyphData;
    int glyphCnt;
    
    printf("Wczytuje : %s \n", newFileName.c_str());
    FILE* f = fopen(newFileName.c_str(), "rb");
    if (f) {
        fread(&mh, sizeof(mainHeader_t), 1, f);        
        printf("Glowny naglowek:\n");
        printf("sygnatura : %s\n", mh.signature);
        printf("Wersja : %d\n", mh.version);
        if (mh.version == 1) {
            fread(&v1, sizeof(versionHeader1_t), 1, f);
            printf("Dane dla wersji 1:\n");
            printf("Font first: %d , last : %d , yAdvance : %d \n", v1.font.first, v1.font.last, v1.font.yAdvance);
            printf("Bitmap start : %d , size : %d \n", v1.bitmapStart, v1.bitmapSize);
            printf("Glyph start : %d , size : %d \n", v1.glyphStart, v1.glyphSize);

            bitmapBytes = (uint8_t*)malloc(v1.bitmapSize);
            if (bitmapBytes == nullptr) {
                printf("[ERROR] bitmap data malloc failed. \n");
                fclose(f);
                exit(EXIT_FAILURE);
            };
            glyphData = (glyph_t*)malloc(v1.glyphSize);
            if (glyphData == nullptr) {
                printf("[ERROR] glyph data malloc failed. \n");
                fclose(f);
                exit(EXIT_FAILURE);
            };

            //wczytaj bitmape
            fseek(f, v1.bitmapStart, SEEK_SET);
            for (uint32_t i = 0; i < v1.bitmapSize; i++) {
                bitmapBytes[i] = fgetc(f);
            };
/*
            //wyswietl bajty z bitmapy
            printf("---BITMAP---- \n");
            for (uint32_t i = 0; i < v1.bitmapSize; i++) {              
                printf("%x \t", bitmapBytes[i]);
                if (i % 10 == 0) printf("\n");
            };
            printf("\n");
*/
            //wczytaj dane glyph
            glyphCnt = v1.glyphSize / sizeof(glyph_t);
            if (glyphCnt < 1) {
                printf("Error : Plik powienien miec przynajmniej 1 element typu glyph \n");
                return;
            };
            fseek(f, v1.glyphStart, SEEK_SET);
            for (int i = 0; i < glyphCnt; i++) {
                fread(&g, sizeof(glyph_t), 1, f);
                glyphData[i] = g;
            };

            //wyswietl dane glyph
            char ch;
            uint16_t ba; // bitmap offset aktualny;
            uint16_t bn; // bitmap ofdset nastepny;
            char hb[10]; // hex buff
            uint8_t ls;
            string sb; // ciag bajtow dla danego glypha
      
            printf("---GLYPH---- \n");
            printf("lp) chr B.off Wid Hei xAdv xOff yOff \n");
            for (int i = 0; i < glyphCnt; i++) {
                g = glyphData[i];
                ch = v1.font.first + i;
                printf("%2d) |%c| %5d %3d %3d %4d %4d %4d\n", i + 1, ch, g.bitmapOffset, g.width, g.height, g.xAdvance, g.xOffset, g.yOffset);
                ba = g.bitmapOffset;
                if (i < glyphCnt - 1) bn = glyphData[i + 1].bitmapOffset;
                else bn = v1.bitmapSize;
                sb = "";
                ls = 0;
                for (int j = ba; j < bn; j++) {
                    if (ls == 10) {
                        sb += "   ";
                    };
                    if (ls == 20) {
                        sb += '\n';
                        ls = 1;
                    }
                    else ls++;
                    //sb += to_string(bitmapBytes[j]);
                    //itoa(bitmapBytes[j], hb, 16);
                    sprintf(hb, "%2X", bitmapBytes[j]);
                    sb += hb;
                    sb += " ";
                };
                printf("%s\n\n", sb.c_str());                 
            };
            free(bitmapBytes);
            free(glyphData);
        }
        else {
            printf("Niezana wersja naglowka.\n");
        }
        fclose(f);
    }
    else {
        printf("Nie moge otworzyc utworzonego pliku. \n");
    };
};
//------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    if (argc == 1) {
        printf("Uzycie: \n");
        printf("%s [nazwa pliku].f  -> konwersja do binarki [nazwa pliku].f2 \n", argv[0]);
        printf("%s [nazwa pliku].f2 -> testowy odczyt binarki. \n", argv[0]);
        printf("%s -all , konwersja wszystkich plikow *.f w bierzacym katalogu. \n", argv[0]);
        printf("%s -ver , aktualna wersja narzedzia.\n", argv[0]);
        printf("Wygenerowany plik bedzie mial rozszezenie *.f2\n");
        return 0;
    };

    if (argc >= 2){
        if (std::string(argv[1]) == "-ver") {
            printf("%s %s \n", argv[0], ver.c_str());
        }
        else if (std::string(argv[1]) == "-all") {
            createListToProcess();
            if (listOfFiles.size() > 0) {
                for (string element : listOfFiles) {   
                    printf("Przetwarzam : %s\n", element.c_str());
                    inputData = readInputData((char*)element.c_str(), &inputError, &inputSize);//wczytaj dane
                    if (inputError) {
                        // process error
                        inputErrorInfo();
                        free(inputData);
                        return 0;
                    }
                    else {
                        // process data
                        inputFileNameBase = getBaseFilename((char*)element.c_str());
                        strData.clear();
                        strData.append(inputData);//wczytane dane do stringa, tak latwiej
                        free(inputData);
                        //prepareStringData(); //usun smieci , utworz bloki do dalszego parsowania
                        if (!prepareStringData()) { //usun smieci , utworz bloki do dalszego parsowania
                            printf("Uszkodzone lub niespojne dane wejsciowe.\n");
                            return 0;
                        };
                        if (outputWriteHeader(inputFileNameBase.c_str())) {
                            outputWriteBitmapData();
                            outputWriteGlyphData();
                            outputWriteFontData();
                            outputUpdateHeader(); //...i zamknij plik.
                        }
                        else {
                            printf("Nie moge otworzyc pliku do zapisu.\n");
                        };
                    };//input read ok
                };//for element in list
            }//file list size > 0
            else {
                printf("Lista plikow pusta. \n");
            }
        }
        else {
            if (isValidFileName(argv[1],&inputType)) {
                //printf("Typ pliku : %d\n", inputType);
                //parametr moze byc plikiem

                if (inputType == FILE_TYPE_IS_NOT_SUPORTED) {
                    // process error
                    inputErrorInfo();
                    free(inputData);
                    return 0;
                }
                else {
                    // process input
                    inputData = readInputData(argv[1], &inputError, &inputSize);//wczytaj dane
                    if (inputError ) {
                        // process error
                        inputErrorInfo();
                        free(inputData);
                        return 0;
                    }
                    inputFileNameBase = getBaseFilename(argv[1]);
                    switch (inputType) {
                    case FILE_TYPE_IS_INPUT:
                        //proces data
                        strData.clear();
                        strData.append(inputData);//wczytane dane do stringa, tak latwiej
                        free(inputData);

                        //prepareStringData(); //usun smieci , utworz bloki do dalszego parsowania
                        if (!prepareStringData()) { //usun smieci , utworz bloki do dalszego parsowania
                            printf("Uszkodzone lub niespojne dane wejsciowe.\n");
                            return 0;
                        };
                        if (outputWriteHeader(inputFileNameBase.c_str())) {
                            outputWriteBitmapData();
                            outputWriteGlyphData();
                            outputWriteFontData();
                            outputUpdateHeader(); //...i zamknij plik.      
                            //readTest();
                        }
                        else {
                            printf("Nie moge otworzyc pliku do zapisu.\n");
                        };
                        break;
                    case FILE_TYPE_IS_OUTPUT:
                        readTest();                            
                        break;
                    default :
                        break;
                    };
                };
            }else {
                //nie jest plilkiem
                printf("Zla skladnia.\n");
            };
        };
    };//argc >=2;
    return 0;
}
