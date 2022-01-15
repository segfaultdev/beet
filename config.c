#include <config.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>

int cfg_strcmp(const char *str_1, const char *str_2) {
  while (*str_1 && !isspace(*str_1) && *str_1 != '.') {
    if (*str_1 != *str_2) break;
    str_1++, str_2++;
  }

  return (int)(*str_1) - (int)(*str_2);
}

int cfg_atoi(const char *str) {
  if (!str) return 0;
  
  int value = 0;
  int is_neg = 0;
  
  if (*str == '-') {
    is_neg = 1;
    str++;
  }
  
  while (*str && !isspace(*str)) {
    value = (value * 10) + (*str - '0');
    str++;
  }
  
  return value;
}

ssize_t cfg_find_raw(FILE *file, const char *name) {
  size_t length = strlen(name);
  char buffer[length + 1];
  
  fseek(file, 0, SEEK_END);
  size_t size = ftell(file);
  
  for (size_t i = 0; i < size; i++) {
    fseek(file, i, SEEK_SET);
    fread(buffer, 1, length, file);
    
    buffer[length] = '\0';
    
    if (!cfg_strcmp(buffer, name)) {
      return i;
    }
  }
  
  return -1;
}

int cfg_find_str(FILE *file, const char *name, char *ptr) {
  if (!file || !name || !ptr) return 0;
  
  ssize_t offset = cfg_find_raw(file, name);
  if (offset < 0) return 0;
  
  offset += strlen(name) + 1;
  char c;
  
  fseek(file, offset, SEEK_SET);
  
  for (;;) {
    int value = fread(&c, 1, 1, file);
    if (!value || !isspace(c)) break;
    
    offset++;
  }
  
  fseek(file, offset, SEEK_SET);
  int in_string = 0;
  
  for (;;) {
    int value = fread(&c, 1, 1, file);
    
    if (!value || (isspace(c) && !in_string)) {
      *ptr = '\0';
      break;
    } else if (c == '"') {
      in_string = !in_string;
    } else {
      *(ptr++) = c;
    }
  }
  
  return 1;
}

int cfg_find_int(FILE *file, const char *name, int *ptr) {
  if (!file || !name || !ptr) return 0;
  
  char buffer[32];
  if (!cfg_find_str(file, name, buffer)) return 0;
  
  if (!strcmp(buffer, "true")) {
    *ptr = 1;
  } else if (!strcmp(buffer, "false")) {
    *ptr = 0;
  } else {
    *ptr = cfg_atoi(buffer);
  }
  
  return 1;
}

int cfg_edit_str(FILE *file, const char *name, const char *ptr) {
  int index = cfg_find_raw(file, name);
  char c = '\n';
  
  if (index >= 0) {
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    
    int end = index + strlen(name) + 1;
    
    for (;;) {
      fseek(file, end, SEEK_SET);
      
      int value = fread(&c, 1, 1, file);
      if (!value || !isspace(c)) break;
      
      end++;
    }
    
    for (;;) {
      fseek(file, end, SEEK_SET);
      
      int value = fread(&c, 1, 1, file);
      if (!value || isspace(c)) break;
      
      end++;
    }
    
    if (c == '\n') end++;
    
    char *buffer = calloc(size - end, 1);
    if (!buffer) return 0;
    
    fseek(file, end, SEEK_SET);
    fread(buffer, 1, size - end, file);
    
    fflush(file);
    
    if (ftruncate(fileno(file), index) < 0) return 0;
    fflush(file);
    
    fseek(file, 0, SEEK_END);
    fwrite(buffer, 1, size - end, file);
    
    fflush(file);
    free(buffer);
  }
  
  fseek(file, -1, SEEK_END);
  if (!fread(&c, 1, 1, file)) c = '\n';
  
  if (c != '\n') {
    fseek(file, 0, SEEK_END);
    fputc('\n', file);
  }
  
  int length = strlen(ptr);
  
  for (int i = 0; i < length; i++) {
    if (isspace(ptr[i])) {
      if (fprintf(file, "%s: \"%s\"\n", name, ptr) < 0) return 0;
      
      fflush(file);
      return 1;
    }
  }
  
  if (fprintf(file, "%s: %s\n", name, ptr) < 0) return 0;
  
  fflush(file);
  return 1;
}

int cfg_edit_int(FILE *file, const char *name, int value) {
  char buffer[32];
  sprintf(buffer, "%d", value);
  
  return cfg_edit_str(file, name, buffer);
}
