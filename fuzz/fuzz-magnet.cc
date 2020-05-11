#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libtransmission/transmission.h>

int isbanned(char c) {
  if (c == ' ') return true;
  if (c == '"') return true;
  if (c == '<') return true;
  if (c == '#') return true;
  if (c == '|') return true;

  return false;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  for (size_t i = 0; i < Size; i++)
    if (iscntrl(Data[i]) || isbanned(Data[i]))
      return EXIT_FAILURE;

  char *uri = (char *)calloc(1, Size + 1);
  strncpy(uri, (const char *)Data, Size);

  tr_ctor *ctor = tr_ctorNew(NULL);
  tr_ctorSetMetainfoFromMagnetLink(ctor, uri);

  tr_info inf;
  tr_parse_result err = tr_torrentParse(ctor, &inf);

  free(uri);
  tr_ctorFree(ctor);
  tr_metainfoFree(&inf);

  return err;
}
