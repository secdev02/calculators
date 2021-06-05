//
// This file is part of Alpertron Calculators.
//
// Copyright 2017-2021 Dario Alejandro Alpern
//
// Alpertron Calculators is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Alpertron Calculators is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Alpertron Calculators.  If not, see <http://www.gnu.org/licenses/>.
//
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bignbr.h"
#include "expression.h"
#include "factor.h"
#include "showtime.h"
#include "batch.h"

static char *ptrEndBatchFactor;
static char *ptrCurrBatchFactor;
static char *ptrNextBatchFactor;
static bool firstExprProcessed;
int valuesProcessed;
char outputExpr[200000];
#ifdef __EMSCRIPTEN__
char *ptrInputText;
char emptyInputText;
#endif

static void stringToHTML(char **pptrOutput, const char *ptrString)
{
  const char* ptrStr = ptrString;
  int character;
  char *ptrOutput = *pptrOutput;
  for (;;)
  {
    char c = *ptrStr;
    if (c == 0)
    {
      break;    // End of string, so go out.
    }
    if ((c & 0x80) == 0)
    {           // 1-byte UTF-8 character
      character = c;
      ptrStr++;
    }
    else if ((c & 0x60) == 0x40)
    {            // 2-byte UTF-8 character
      character = ((c & 0x1F) * 64) + (*(ptrStr + 1) & 0x3F);
      ptrStr += 2;
    }
    else
    {            // 3-byte UTF-8 character
      character = ((c & 0x1F) * 0x1000) +
        ((*(ptrStr + 1) & 0x3F) * 0x40) +
        (*(ptrStr + 2) & 0x3F);
      ptrStr += 3;
    }
    if ((character >= ' ') && (character < 127) && (character != '<') &&
      (character != '>') && (character != '&'))
    {  // Safe to copy character.
      *ptrOutput = (char)character;
      ptrOutput++;
    }
    else
    {  // Insert HTML entity.
      *ptrOutput = '&';
      ptrOutput++;
      *ptrOutput = '#';
      ptrOutput++;
      int2dec(&ptrOutput, character);
      *ptrOutput = ';';
      ptrOutput++;
    }
  }
  *pptrOutput = ptrOutput;
}

static enum eExprErr evalExpression(const char *expr, BigInteger *ptrResult)
{
  const char *ptrInputExpr = expr;
  char *ptrOutputExpr = outputExpr;
  while (*ptrInputExpr != 0)
  {
    char c = *ptrInputExpr;
    if (c == ';')
    {
      break;
    }
    // Copy character to output expression.
    *ptrOutputExpr = c;
    ptrOutputExpr++;
    ptrInputExpr++;
  }
  *ptrOutputExpr = 0;   // Append string terminator.
  return ComputeExpression(outputExpr, ptrResult);
}

static void SkipSpaces(char **pptrText)
{
  char *ptrText = *pptrText;
  while ((*ptrText == ' ') || (*ptrText == 9))
  {  // Skip spaces or tabs.
    ptrText++;
  }
  *pptrText = ptrText;
}

static void BatchError(char **pptrOutput, const char *batchText, const char *errorText)
{
  char *ptrOutput = *pptrOutput;
  stringToHTML(&ptrOutput, batchText);
  *ptrOutput = ':';
  ptrOutput++;
  *ptrOutput = ' ';
  ptrOutput++;
  copyStr(&ptrOutput, errorText);
  *pptrOutput = ptrOutput;
  counterC = 0;
}

enum eExprErr BatchProcessing(char *batchText, BigInteger *valueFound, char **pptrOutput, bool *pIsBatch)
{
  int endValuesProcessed;
  char *ptrOutput = output;
  char *NextExpr;
  char *EndExpr;
  char *ptrExprToProcess = NULL;
  const char *ptrConditionExpr = NULL;
  enum eExprErr rc = EXPR_OK;
  char *ptrCharFound;
  char *ptrSrcString;
  char *ptrStartExpr;
  copyStr(&ptrOutput, "2<ul><li>");
  endValuesProcessed = valuesProcessed + 1000;
  if (pIsBatch != NULL)
  {
    *pIsBatch = false;    // Indicate not batch processing in advance.
  }
  if (valuesProcessed == 0)
  {        // Start batch factorization.
    ptrCurrBatchFactor = batchText;
    ptrEndBatchFactor = batchText + strlen(batchText);
    firstExprProcessed = false;
  }
  for (; ptrCurrBatchFactor < ptrEndBatchFactor;
    ptrCurrBatchFactor += (int)strlen(ptrCurrBatchFactor) + 1)
  {  // Get next line.
    char c;
    expressionNbr = 0;
    if (firstExprProcessed == false)
    {
      valueX.nbrLimbs = 0;     // Invalidate variable x and counter c.
      ptrNextBatchFactor = findChar(ptrCurrBatchFactor, '\n');
      if (ptrNextBatchFactor != NULL)
      {
        *ptrNextBatchFactor = 0;    // Indicate end of line.
      }
      counterC = 1;
    }
    // Skip leading spaces.
    ptrSrcString = ptrCurrBatchFactor;
    SkipSpaces(&ptrSrcString);
    c = *ptrSrcString;
    if (c == 0)
    {   // Empty line.
      copyStr(&ptrOutput, "<br>");
    }
    else if (c == '#')
    {   // Copy comment to output, but convert non-safe characters to entities.
      *ptrOutput = '#';
      ptrOutput++;
      stringToHTML(&ptrOutput, ptrCurrBatchFactor + 1);
    }
    else if ((c == 'x') || (c == 'X'))
    {   // Loop format: x=<orig expr>; x=<next expr>; <end expr>; <expr to factor>[; <factor cond>]
#ifdef __EMSCRIPTEN__
      ptrInputText = &emptyInputText;
#endif
      if (!firstExprProcessed)
      {
        ptrCharFound = findChar(ptrSrcString + 1, ';');
        if (ptrCharFound == NULL)
        {
          BatchError(&ptrOutput, batchText,
            lang ? "se esperaban tres o cuatro puntos y comas pero no hay ninguno" :
            "three or four semicolons expected but none found");
          ptrOutput += 4;
          continue;
        }
        ptrStartExpr = ptrSrcString + 1;
        SkipSpaces(&ptrStartExpr);
        if (*ptrStartExpr != '=')
        {
          BatchError(&ptrOutput, batchText,
            lang ? "falta signo igual en la primera expresión" :
            "equal sign missing in first expression");
          ptrOutput += 4;
          continue;
        }
        ptrCharFound = findChar(ptrSrcString + 1, ';');
        expressionNbr = 1;
        rc = evalExpression(ptrStartExpr + 1, valueFound);
        CopyBigInt(&valueX, valueFound);
        if (rc != EXPR_OK)
        {
          textError(&ptrOutput, rc);
          ptrOutput += 4;
          continue;
        }
        ptrStartExpr = ptrCharFound + 1;
        SkipSpaces(&ptrStartExpr);
        if ((*ptrStartExpr != 'x') && (*ptrStartExpr != 'X'))
        {
          BatchError(&ptrOutput, batchText,
            lang ? "falta variable x en la segunda expresión" :
            "variable x missing in second expression");
          ptrOutput += 4;
          continue;
        }
        ptrStartExpr++;               // Skip variable 'x'.
        SkipSpaces(&ptrStartExpr);
        if (*ptrStartExpr != '=')
        {
          BatchError(&ptrOutput, batchText,
            lang ? "falta signo igual en la segunda expresión" :
            "equal sign missing in second expression");
          ptrOutput += 4;
          continue;
        }
        NextExpr = ptrStartExpr + 1;  // Skip equal sign.
        ptrCharFound = findChar(ptrStartExpr, ';');  // Find second semicolon.
        if (ptrCharFound == NULL)
        {      // Third semicolon not found.
          BatchError(&ptrOutput, batchText,
            lang ? "se esperaban tres o cuatro puntos y comas pero solo hay uno" :
            "three or four semicolons expected but there are only one");
          ptrOutput += 4;
          continue;
        }
        EndExpr = ptrCharFound + 1;  // Point to end expression.
        ptrCharFound = findChar(EndExpr, ';');  // Find third semicolon.
        if (ptrCharFound == NULL)
        {      // Third semicolon not found.
          BatchError(&ptrOutput, batchText,
            lang ? "se esperaban tres o cuatro puntos y comas pero solo hay dos" :
            "three or four semicolons expected but there are only two");
          ptrOutput += 4;
          continue;
        }
        ptrExprToProcess = ptrCharFound + 1;
        ptrConditionExpr = findChar(ptrExprToProcess, ';');  // Find optional fourth semicolon. 
        if (ptrConditionExpr != NULL)
        {
          ptrConditionExpr++;
        }
        firstExprProcessed = true;
      }
      else
      {
        NextExpr = findChar(ptrCurrBatchFactor, ';') + 1;  // Point to "x="
        SkipSpaces(&NextExpr);   // Now point to "x"
        NextExpr++;
        SkipSpaces(&NextExpr);   // Now point to "="
        NextExpr++;
        SkipSpaces(&NextExpr);   // Now point to next expression.
        EndExpr = findChar(NextExpr, ';') + 1;
        ptrExprToProcess = findChar(EndExpr, ';') + 1;
        ptrConditionExpr = findChar(ptrExprToProcess, ';');
        if (ptrConditionExpr != NULL)
        {
          ptrConditionExpr++;
        }
      }
      if (pIsBatch != NULL)
      {
        *pIsBatch = true;    // Indicate batch processing.
      }
      while (ptrOutput < &output[(int)sizeof(output) - 200000])
      {      // Perform loop.
        bool processExpression = true;
        expressionNbr = 3;
        rc = evalExpression(EndExpr, valueFound);
        if (rc != EXPR_OK)
        {
          textError(&ptrOutput, rc);
          break;   // Cannot compute end expression, so go out.
        }
        if (BigIntIsZero(valueFound))
        {    // result is zero: end of loop
          firstExprProcessed = false;
          break;
        }
        if (valuesProcessed >= endValuesProcessed)
        {
          output[0] = '6';  // Show Continue button.
          break;
        }
        if (ptrConditionExpr != NULL)
        {
          expressionNbr = 5;
          rc = evalExpression(ptrConditionExpr, valueFound);
          if (rc == EXPR_OK)
          {
            if (BigIntIsZero(valueFound))
            {   // Do not compute factor expression if condition is false.
              processExpression = false;
            }
          }
          else
          {
            textError(&ptrOutput, rc);
            if ((rc == EXPR_SYNTAX_ERROR) || (rc == EXPR_VAR_OR_COUNTER_REQUIRED))
            {   // Do not show multiple errors.
              break;
            }
            processExpression = false;
          }
        }
        if (processExpression)
        {
          expressionNbr = 4;
          rc = evalExpression(ptrExprToProcess, valueFound);
          if (rc == EXPR_OK)
          {
            batchCallback(&ptrOutput);
          }
          else
          {
            textError(&ptrOutput, rc);
          }
          if ((rc == EXPR_SYNTAX_ERROR) || (rc == EXPR_VAR_OR_COUNTER_REQUIRED))
          {   // Do not show multiple errors.
            break;
          }
        }
        expressionNbr = 2;
        rc = evalExpression(NextExpr, valueFound);
        if (rc != EXPR_OK)
        {
          textError(&ptrOutput, rc);
          break;   // Cannot compute next expression, so go out.
        }
        CopyBigInt(&valueX, valueFound);
        if (processExpression)
        {
          counterC++;
          copyStr(&ptrOutput, "</li><li>");
          valuesProcessed++;
        }
      }
      if (valuesProcessed >= endValuesProcessed)
      {
        break;
      }
    }
    else
    {       // Factor expression (not loop).
#ifdef __EMSCRIPTEN__
      ptrInputText = ptrSrcString;
#endif
      if (valuesProcessed >= endValuesProcessed)
      {
        output[0] = '6';  // Show Continue button.
      }
      rc = ComputeExpression(ptrSrcString, valueFound);
      if (rc == EXPR_OK)
      {
        batchCallback(&ptrOutput);
      }
      else
      {
        textError(&ptrOutput, rc);
      }
      counterC = 2;
      copyStr(&ptrOutput, "</li><li>");
      valuesProcessed++;
    }
    if (counterC == 1)
    {
      copyStr(&ptrOutput, "</li>");
    }
    if (ptrOutput >= &output[(int)sizeof(output) - 200000])
    {
      output[0] = '6';     // Show Continue button.
      break;
    }
    if ((*ptrCurrBatchFactor & 0xDF) == 'x')
    {      // Loop mode.
      if (ptrConditionExpr != NULL)
      {
        ptrCurrBatchFactor += (int)strlen(ptrConditionExpr) + 1;
        ptrConditionExpr = NULL;
      }
      else
      {
        ptrCurrBatchFactor += (int)strlen(ptrExprToProcess) + 1;
      }
    }
    valueX.nbrLimbs = 0;     // Invalidate variable x and counter c.
  }
  ptrOutput -= 4;            // Erase start tag <li> without contents.
  if (counterC == 1)
  {
    ptrOutput--;             // Erase extra charaacter of </li>
    copyStr(&ptrOutput, lang ? "No hay valores para la expresión ingresada.":
                        "There are no values for the requested expression.");
  }
  copyStr(&ptrOutput, "</ul>");
  *pptrOutput = ptrOutput;
  return rc;
}

char *findChar(char *str, char c)
{
  char* ptrStr = str;
  while (*ptrStr != '\0')
  {
    if (*ptrStr == c)
    {
      return ptrStr;
    }
    ptrStr++;
  }
  return NULL;
}
