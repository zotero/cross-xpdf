//========================================================================
//
// pdftotext.cc
//
// Copyright 1997-2013 Glyph & Cog, LLC
//
//========================================================================
#include "GList.h"
#include <aconf.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <map>
#include "gmem.h"
#include "gmempp.h"
#include "parseargs.h"
#include "GString.h"
#include "GlobalParams.h"
#include "Object.h"
#include "Stream.h"
#include "Array.h"
#include "Dict.h"
#include "XRef.h"
#include "Catalog.h"
#include "Page.h"
#include "PDFDoc.h"
#include "TextOutputDev.h"
#include "CharTypes.h"
#include "UnicodeMap.h"
#include "TextString.h"
#include "Error.h"
#include "config.h"
#include "PDFDocEncoding.h"

static int firstPage = 1;
static int lastPage = 0;
static GBool physLayout = gFalse;
static GBool simpleLayout = gFalse;
static GBool tableLayout = gFalse;
static GBool linePrinter = gFalse;
static GBool rawOrder = gFalse;
static double fixedPitch = 0;
static double fixedLineSpacing = 0;
static GBool clipText = gFalse;
static GBool discardDiag = gFalse;
static GBool noPageBreaks = gFalse;
static GBool insertBOM = gFalse;
static GBool quiet = gFalse;
static GBool printVersion = gFalse;
static GBool printHelp = gFalse;
static GBool json = gFalse;
static char datadir[8192] = "";

static ArgDesc argDesc[] = {
        {"-f",           argInt,    &firstPage,        0,
                "first page to convert"},
        {"-l",           argInt,    &lastPage,         0,
                "last page to convert"},
        {"-layout",      argFlag,   &physLayout,       0,
                "maintain original physical layout"},
        {"-simple",      argFlag,   &simpleLayout,     0,
                "simple one-column page layout"},
        {"-table",       argFlag,   &tableLayout,      0,
                "similar to -layout, but optimized for tables"},
        {"-lineprinter", argFlag,   &linePrinter,      0,
                "use strict fixed-pitch/height layout"},
        {"-raw",         argFlag,   &rawOrder,         0,
                "keep strings in content stream order"},
        {"-fixed",       argFP,     &fixedPitch,       0,
                "assume fixed-pitch (or tabular) text"},
        {"-linespacing", argFP,     &fixedLineSpacing, 0,
                "fixed line spacing for LinePrinter mode"},
        {"-clip",        argFlag,   &clipText,         0,
                "separate clipped text"},
        {"-nodiag",      argFlag,   &discardDiag,      0,
                "discard diagonal text"},
        {"-datadir",     argString, datadir,           sizeof(datadir),
                "data directory"},
        {"-json",        argFlag,   &json,             0,
                "output JSON with metadata, layout and rich text"},
        {"-nopgbrk",     argFlag,   &noPageBreaks,     0,
                "don't insert page breaks between pages"},
        {"-q",           argFlag,   &quiet,            0,
                "don't print any messages or errors"},
        {"-v",           argFlag,   &printVersion,     0,
                "print copyright and version info"},
        {"-h",           argFlag,   &printHelp,        0,
                "print usage information"},
        {"-help",        argFlag,   &printHelp,        0,
                "print usage information"},
        {"--help",       argFlag,   &printHelp,        0,
                "print usage information"},
        {"-?",           argFlag,   &printHelp,        0,
                "print usage information"},
        {NULL}
};

// From https://stackoverflow.com/a/33799784
std::string escape_json(const std::string &s) {
    std::ostringstream o;
    for (std::string::const_iterator c = s.begin(); c != s.end(); c++) {
        switch (*c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if ('\x00' <= *c && *c <= '\x1f') {
                    o << "\\u"
                      << std::hex << std::setw(4) << std::setfill('0') << (int)*c;
                } else {
                    o << *c;
                }
        }
    }
    return o.str();
}

static void printInfoJSON(FILE *f, Dict *infoDict, UnicodeMap *uMap) {
  GString *s1;
  GBool isUnicode;
  Unicode u;
  char buf[9];
  int i, n;

  bool firstE = true;
  for (int k = 0; k < infoDict->getLength(); k++) {
    const std::string keyStr = escape_json(infoDict->getKey(k));
    if (!keyStr.length()) continue;
    Object obj;
    infoDict->getVal(k, &obj);
    if (obj.isString()) {
      if (firstE) firstE = false; else fprintf(f, ",");
      s1 = obj.getString();
      if ((s1->getChar(0) & 0xff) == 0xfe &&
          (s1->getChar(1) & 0xff) == 0xff) {
        isUnicode = gTrue;
        i = 2;
      } else {
        isUnicode = gFalse;
        i = 0;
      }
      std::string valueStr = "";
      while (i < obj.getString()->getLength()) {
        if (isUnicode) {
          u = ((s1->getChar(i) & 0xff) << 8) |
              (s1->getChar(i + 1) & 0xff);
          i += 2;
        } else {
          u = pdfDocEncoding[s1->getChar(i) & 0xff];
          ++i;
        }
        n = uMap->mapUnicode(u, buf, sizeof(buf));
        buf[n] = '\0';
        valueStr += escape_json(buf);
      }
      fprintf(f, "\"%s\":\"%s\"", keyStr.c_str(), valueStr.c_str());
    }
  }
}

void printDocJSON(FILE *f, PDFDoc *doc, TextOutputDev *textOut, int first, int last, UnicodeMap *uMap) {
  double xMin, yMin, xMax, yMax;
  TextPage *text;
  GList *cols, *pars, *lines, *words;
  TextColumn *col;
  TextParagraph *par;
  TextLine *line;
  TextWord *word;
  int colIdx, parIdx, lineIdx, wordIdx;

  std::map<std::string, int> fonts;
  std::map<std::string, int> colors;
  std::map<std::string, int>::iterator it;

  fprintf(f, "{\"metadata\":{");

  Object info;
  doc->getDocInfo(&info);
  if (info.isDict()) {
    printInfoJSON(f, info.getDict(), uMap);
  }

  fprintf(f, "},");
  fprintf(f, "\"totalPages\":%d,", doc->getNumPages());
  fprintf(f, "\"pages\":[");
  bool firstP = true;
  for (int page = first; page <= last; ++page) {
    if (firstP) firstP = false; else fprintf(f, ",");
    fprintf(f, "[%g,%g,[", doc->getPageMediaWidth(page), doc->getPageMediaHeight(page));
    doc->displayPage(textOut, page, 72, 72, 0, gTrue, gFalse, gFalse);
    text = textOut->takeText();

    cols = text->makeColumns();

    bool firstF = true;
    for (colIdx = 0; colIdx < cols->getLength(); ++colIdx) {
      col = (TextColumn *) cols->get(colIdx);
      if (firstF) firstF = false; else fprintf(f, ",");
      fprintf(f, "[[");
      bool firstB = true;
      pars = col->getParagraphs();
      for (parIdx = 0; parIdx < pars->getLength(); ++parIdx) {
        par = (TextParagraph *) pars->get(parIdx);
        if (firstB) firstB = false; else fprintf(f, ",");
        fprintf(f, "[%g,%g,%g,%g,[", par->getXMin(), par->getYMin(), par->getXMax(), par->getYMin());
        bool firstL = true;
        lines = par->getLines();
        for (lineIdx = 0; lineIdx < lines->getLength(); ++lineIdx) {
          line = (TextLine *) lines->get(lineIdx);
          if (firstL) firstL = false; else fprintf(f, ",");
          fprintf(f, "[[");
          bool firstW = true;

          words = line->getWords();

          for (wordIdx = 0; wordIdx < words->getLength(); ++wordIdx) {
            word = (TextWord *) words->get(wordIdx);

            if (firstW) firstW = false; else fprintf(f, ",");
            word->getBBox(&xMin, &yMin, &xMax, &yMax);
            const std::string myString = escape_json(word->getText()->getCString());

            // Instead of the actual RGB offsets we only output a unique color number
            double dr, dg, db;
            int r, g, b;
            char colorStr[256];
            int color_nr = 0;

            word->getColor(&dr, &dg, &db);
            r = 255.0 * dr;
            g = 255.0 * dg;
            b = 255.0 * db;

            sprintf(colorStr, "%02x%02x%02x", r, g, b);

            colors.insert(std::make_pair(colorStr, colors.size()));
            it = colors.find(colorStr);
            if (it != colors.end()) {
              color_nr = it->second;
            }

            // Instead of the actual font names we only output a unique font number
            int font_nr = 0;
            TextFontInfo *fontInfo = word->getFontInfo();

            if (fontInfo && fontInfo->getFontName()) {
              const std::string fontName = escape_json(fontInfo->getFontName()->getCString());
              fonts.insert(std::make_pair(fontName, fonts.size()));
              it = fonts.find(fontName);
              if (it != fonts.end()) {
                font_nr = it->second;
              }
            }

            fprintf(f,
                    "["
                            "%g,"
                            "%g,"
                            "%g,"
                            "%g,"
                            "%g,"
                            "%d,"
                            "%g,"
                            "%d,"
                            "%d,"
                            "%d,"
                            "%d,"
                            "%d,"
                            "%d,"
                            "\"%s\""
                            "]",
                    xMin,
                    yMin,
                    xMax,
                    yMax,
                    word->getFontSize(),
                    word->getSpaceAfter(),
                    word->getBaseline(),
                    word->getRotation(),
                    word->isUnderlined(),
                    fontInfo->isBold(),
                    fontInfo->isItalic(),
                    color_nr,
                    font_nr,
                    myString.c_str()
            );
          }
          fprintf(f, "]]");
        }
        fprintf(f, "]]");
      }
      fprintf(f, "]]");
    }
    fprintf(f, "]]");
  }
  fprintf(f, "]}");
}

int main(int argc, char *argv[]) {
  PDFDoc *doc;
  GString *fileName;
  GString *textFileName;
  TextOutputControl textOutControl;
  TextOutputDev *textOut;
  UnicodeMap *uMap;
  GBool ok;
  int exitCode;
  FILE *f;

  exitCode = 99;

  // parse args
  ok = parseArgs(argDesc, &argc, argv);
  if (!ok || argc != 3 || printVersion || printHelp) {
    fprintf(stderr, "This is a custom Xpdf pdftotext build. Please use the original version!\n");
    fprintf(stderr, "pdftotext version %s\n", xpdfVersion);
    fprintf(stderr, "%s\n", xpdfCopyright);
    if (!printVersion) {
      printUsage("pdftotext", "<PDF-file> <text-file>", argDesc);
    }
    goto err0;
  }

  fileName = new GString(argv[1]);

  // read config file
  globalParams = new GlobalParams("");

  globalParams->setTextEncoding("UTF-8");

  globalParams->setTextEOL("unix");

  if (noPageBreaks) {
    globalParams->setTextPageBreaks(gFalse);
  }
  if (quiet) {
    globalParams->setErrQuiet(quiet);
  }

  // get mapping to output encoding
  if (!(uMap = globalParams->getTextEncoding())) {
    error(errConfig, -1, "Couldn't get text encoding");
    delete fileName;
    goto err1;
  }

  globalParams->scanEncodingDirs(datadir);


  doc = new PDFDoc(fileName);

  if (!doc->isOk()) {
    exitCode = 1;
    goto err2;
  }

  // construct text file name
  textFileName = new GString(argv[2]);

  // get page range
  if (firstPage < 1) {
    firstPage = 1;
  }
  if (lastPage < 1 || lastPage > doc->getNumPages()) {
    lastPage = doc->getNumPages();
  }

  // write text file
  if (tableLayout) {
    textOutControl.mode = textOutTableLayout;
    textOutControl.fixedPitch = fixedPitch;
  } else if (physLayout) {
    textOutControl.mode = textOutPhysLayout;
    textOutControl.fixedPitch = fixedPitch;
  } else if (simpleLayout) {
    textOutControl.mode = textOutSimpleLayout;
  } else if (linePrinter) {
    textOutControl.mode = textOutLinePrinter;
    textOutControl.fixedPitch = fixedPitch;
    textOutControl.fixedLineSpacing = fixedLineSpacing;
  } else if (rawOrder) {
    textOutControl.mode = textOutRawOrder;
  } else {
    textOutControl.mode = textOutReadingOrder;
  }
  textOutControl.clipText = clipText;
  textOutControl.discardDiagonalText = discardDiag;
  textOutControl.insertBOM = insertBOM;

  // output JSON
  if (json) {
    if (!(f = fopen(textFileName->getCString(), "wb"))) {
      error(errIO, -1, "Couldn't open text file '{0:t}'", textFileName);
      exitCode = 2;
      goto err3;
    }
    textOut = new TextOutputDev(NULL, &textOutControl, gFalse);
    if (textOut->isOk()) {
      printDocJSON(f, doc, textOut, firstPage, lastPage, uMap);
    }
    fclose(f);
  } // output text
  else {
    textOut = new TextOutputDev(textFileName->getCString(), &textOutControl, gFalse);
    if (textOut->isOk()) {
      doc->displayPages(textOut, firstPage, lastPage, 72, 72, 0, gTrue, gFalse, gFalse);
    } else {
      delete textOut;
      exitCode = 2;
      goto err3;
    }
  }

  delete textOut;

  exitCode = 0;

  // clean up
  err3:
  delete textFileName;
  err2:
  delete doc;
  uMap->decRefCnt();
  err1:
  delete globalParams;
  err0:

  // check for memory leaks
  Object::memCheck(stderr);
  gMemReport(stderr);

  return exitCode;
}
