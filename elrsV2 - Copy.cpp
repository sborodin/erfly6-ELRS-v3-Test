#include <stdio.h>
#include "crossfire.h"
#include "../er9x.h"
#include "../lcd.h"
#include "../menus.h"
#include "../en.h"
#include "../voice.h"

struct FieldProps {
  uint16_t nameOffset;     
  uint8_t valuesLength;   
  uint16_t valuesOffset;   
  uint8_t nameLength;     
  uint8_t parent;
  uint8_t type : 4;       
  uint8_t value : 4; 
  uint8_t valueMax:4;       
  uint8_t id : 8;         
  uint8_t hidden : 1;     
  uint8_t spare : 2;     
} PACKED;

uint8_t badPkt=0;
uint32_t goodPkt=0;

FieldProps fields[25];
uint8_t curfieldId = 1;
uint8_t curselectIdx = 0;
uint8_t curFieldChunk =0;
uint8_t curNumSelection = 0;
static uint32_t fieldTimeout= 0;
static uint32_t linkstatTimeout = 0;
uint8_t fieldData[72]; 
uint8_t fieldDataLen = 0;
int8_t expectedChunks = -1;
uint8_t statusComplete = 0; 
char fields_count=0;
char allParamsLoaded=0;
bool itemmodified = false;
bool bMenuShown = false;

#define COL1_CHAR_LEN 12
#define COL2_CHAR_LEN 9
#define COL2           12*FW

static uint8_t reusableBuffer[0x800];
static uint16_t reusableBufferOffset = 0;

#define  lcdDrawText lcd_putsAtt
#define lcdDrawSizedText lcd_putsnAtt  

void crossfireTelemetryPush4(const uint8_t cmd, const uint8_t third, const uint8_t fourth)
{
  uint8_t crsfPushData[4] =  { deviceId, handsetId, third, fourth };
  crossfireTelemetryPush(cmd, crsfPushData, 4);
}

void crossfireTelemetryPing()
{
  {
    uint8_t crsfPushData[2] = { 0x00, 0xEA };
    crossfireTelemetryPush(0x28, crsfPushData, 2);
  }
}

void parseDeviceInfoMessage(uint8_t* data)
{
  uint8_t offset;
  uint8_t id = data[2];
  offset = strlen((char*)&data[3]) + 1 + 3; 
  if ( deviceId == id)
  { 
    if(data[offset+12]>0)
    {
      if(fields_count==0)
      {
        crossfireTelemetryPush4(0x2C, curfieldId, curFieldChunk); 
        fieldTimeout   = g_tmr10ms+1;
      }
      fields_count = data[offset+12];
    }
  }
}

#define fieldTextSelectionSave( field)  crossfireTelemetryPush4(0x2D, field->id, field->value)

void parseParameterInfoMessage(uint8_t* data, uint8_t length)
{
#if 1
 if (data[2] != deviceId || data[3] != curfieldId) {
    fieldDataLen = 0; 
    curFieldChunk = 0;
    return;
  }
  if (fieldDataLen == 0) {
    expectedChunks = -1;
  }
  FieldProps* field = &fields[curfieldId - 1];
  uint8_t chunks = data[4];
  if (field == 0 || (chunks != expectedChunks && expectedChunks != -1)) {
    return; 
  }
  expectedChunks = chunks - 1;
  for (uint32_t i=5; i< length; i++) {
    fieldData[fieldDataLen++] = data[i];
  }
  if (chunks > 0) {
    curFieldChunk ++;
    statusComplete = 0;
  } else { 
    curFieldChunk = 0;
    if (fieldDataLen < 4) { 
      fieldDataLen = 0; 
      return; 
    }

    field->id = curfieldId;
    uint8_t parent = fieldData[0]; 
    uint8_t type = fieldData[1] & 0x7F;
    uint8_t hidden = (fieldData[1] & 0x80) ? 1 : 0; 
    uint8_t offset;
    if (field->nameLength != 0) { 
      if (field->parent != parent || field->type != type || field->hidden != hidden) {
        fieldDataLen = 0; 
        return; 
      }
    }
    field->parent = parent;
    field->type = type;
    field->hidden = hidden;
    offset = strlen((char*)&fieldData[2]) + 1 + 2; 
    if (field->nameLength == 0) {  
      field->nameLength = offset - 3;

      if(field->type != 13 && field->nameLength > COL1_CHAR_LEN)
      {
         field->nameLength = COL1_CHAR_LEN;
      }
      field->nameOffset = reusableBufferOffset;
      memcpy(&reusableBuffer[reusableBufferOffset], &fieldData[2], field->nameLength); 
      reusableBufferOffset += field->nameLength;
    }
    else
    {
        memcpy(&reusableBuffer[field->nameOffset], &fieldData[2], field->nameLength); 
    }
    if (field->type ==9   ) {
       
       int len=strlen((char*)&fieldData[offset]);
       if(!allParamsLoaded)
       {
         if(field->valuesLength ==0)
         {
           field->valuesOffset = reusableBufferOffset;
            reusableBuffer[reusableBufferOffset++]=COL2_CHAR_LEN;

           int lastStart = 0;

            for(int i=0;i<len;i++)
            { 

              if(fieldData[offset+i]==';'||i==len-1)
              {
                  int flen = i-lastStart;
                  if(len-1==i) flen +=1;
                  if(flen>COL2_CHAR_LEN)
                  {
                    flen = COL2_CHAR_LEN;
                  }
                  memcpy(&reusableBuffer[reusableBufferOffset], &fieldData[offset+lastStart], flen);
                  reusableBufferOffset+=flen;
                  for(int  j=flen;j<COL2_CHAR_LEN;j++)
                  {
                      reusableBuffer[reusableBufferOffset++] = ' ';
                  }
                  lastStart =  i+1;
                  field->valueMax++;
              }
            }
            reusableBuffer[reusableBufferOffset++]=0;
            field->valuesLength = reusableBufferOffset-field->valuesOffset;
          }
        }
        offset += len + 1;
        field->value = fieldData[offset];
        if(!allParamsLoaded)
           curNumSelection ++;
    }
    else  if (field->type ==13   )
    {
        field->value = fieldData[offset++];
    
        if(!allParamsLoaded)
        {
           offset++;
           int len=strlen((char*)&fieldData[offset]);
            int flen = len;
            if(flen>COL2_CHAR_LEN)
               flen = COL2_CHAR_LEN;
            field->valuesOffset = reusableBufferOffset;
            memcpy(&reusableBuffer[reusableBufferOffset], &fieldData[offset], flen);
            reusableBufferOffset+=flen;
            field->valuesLength = flen;
           offset += len + 1;
           curNumSelection ++;
       }

    }
    
    if(!itemmodified)
    {
      curfieldId ++; //1 + (curfieldId % fields_count);
      if (curfieldId > fields_count) {

        allParamsLoaded = 1;
        curfieldId = 1;
      } 
    }
    else
    {
        itemmodified = false;
    }

    statusComplete = 1;
    fieldDataLen = 0; 
  }
  #endif
}

void runCrossfireTelemetryCallback(uint8_t command, uint8_t* data, uint8_t length)
{
  if(!bMenuShown) return;
  if (command == 0x29)
  {
    parseDeviceInfoMessage(data);
  }
  else
  if (command == 0x2B)
  {
    parseParameterInfoMessage(data, length);
    if (allParamsLoaded < 1 || statusComplete == 0)
    {
      fieldTimeout = 0; 
    }
  }
}

void executeMenu(uint8_t event)
{
  FieldProps *field = &fields[curselectIdx];
  int y = 0; 
  lcdDrawSizedText(0, y,(char *)&reusableBuffer[field->nameOffset], field->nameLength, 0);
  y += FH;
  lcdDrawText(0,y,STR_CRST_FIELD_ID,0);
  lcd_outdezAtt(10 * FW,y,field->id,0);
  y += FH;
  lcdDrawText(0,y,STR_VALUE,0);
  lcd_outdezAtt(10 * FW,y,field->value,0);
  y += FH;
  lcdDrawText(0,y,STR_CRSF_COMMAND_SENT,0);
  if ( event == EVT_KEY_FIRST(KEY_EXIT) )
  {
    resetElrsMenuVariables();
    popMenu(false) ;
  }
}

void crossfileMenu(MState2 &mstate2,uint8_t  event, uint8_t sub,  uint8_t subN, uint8_t y )
{
  if(!bMenuShown)
  {
    bMenuShown  = true;
  }
  uint8_t attr =0;
  if( fields_count <=0)
  {
    mstate2.check_columns(event, 1);
    lcd_putsAtt(0,y,PSTR(STR_LOADING),0);
    if(linkstatTimeout < g_tmr10ms )
    {
      linkstatTimeout = g_tmr10ms + 10;
      crossfireTelemetryPing();
    }
  }
  else
  {
    {
      if(!allParamsLoaded || itemmodified )
      { 
        if(fieldTimeout < g_tmr10ms )
        {
          fieldTimeout = g_tmr10ms +5;
          crossfireTelemetryPush4(0x2C, curfieldId, curFieldChunk); 
        }
      }
    }
    mstate2.check_columns(event, curNumSelection);
    int startDrawIndx = 0;
    int drawedNum = 0;
    int type9idx =0;
    if(sub >7)
    {
      startDrawIndx = sub-7;
    }
    int numfield =allParamsLoaded? fields_count:(curfieldId-1);
    for(int i=0;i<numfield;i++)
    {
      FieldProps *field = &fields[i];
      if(field->type ==9||field->type ==13) 
      {
        type9idx ++;
        if(type9idx > startDrawIndx && drawedNum < 7)
        {
          drawedNum ++;
          lcdDrawSizedText(0, y,(char *)&reusableBuffer[field->nameOffset], field->nameLength, 0);    
          if (sub == subN)
          {
            if(field->type ==9)
            {
              uint8_t modvalue =  checkIncDec_0(field->value,  field->valueMax-1);
              attr = InverseBlink;
              if(modvalue !=  field->value)
              {
                field->value  = modvalue;
                fieldTextSelectionSave(field);
                fieldTimeout = g_tmr10ms +100;
                curfieldId  = field->id; 
                curFieldChunk = 0;
                itemmodified = true;
              }
            }
            else 
            {
              lcd_char_inverse( 0, y, DISPLAY_W, 0 ) ;
              if ( Tevent==EVT_KEY_BREAK(KEY_MENU))
              {
                if(field->value<4)
                field->value = 1;
                crossfireTelemetryPush4(0x2D, field->id, field->value);
                curselectIdx = i;
                pushMenu(executeMenu); 
              }
            }
          }   
          if(field->type ==9)
          {
            lcd_putsAttIdx(COL2, y, (char *)&reusableBuffer[field->valuesOffset], field->value, (sub == subN ? InverseBlink : 0));
          }
          else
          {
            lcd_outdezAtt(21 * FW,y,field->id,0);
            lcd_outdezAtt(17 * FW,y,field->value,0);
          }     
          y += FH;
        }   
        attr = 0;
        subN ++; 
      }
    }
  }
}

void resetElrsMenuVariables()
{
  curfieldId = 1;
  curFieldChunk =0;
  curNumSelection = 0;
  fieldDataLen = 0;
  fields_count=0;
  allParamsLoaded=0;
  reusableBufferOffset = 0;
  bMenuShown = false;
}