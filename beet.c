#include <pthread.h>
#include <config.h>
#include <raylib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

const float freqs[] = {
  8, 9, 9, 10, 10, 11, 12, 12, 
  13, 14, 15, 15, 16, 17, 18, 19, 
  21, 22, 23, 24, 26, 28, 29, 31, 
  33, 35, 37, 39, 41, 44, 46, 49, 
  52, 55, 58, 62, 65, 69, 73, 78, 
  82, 87, 92, 98, 104, 110, 117, 123, 
  131, 139, 147, 156, 165, 175, 185, 196, 
  208, 220, 233, 247, 262, 277, 294, 311, 
  330, 349, 370, 392, 415, 440, 466, 494, 
  523, 554, 587, 622, 659, 698, 740, 784, 
  831, 880, 932, 988, 1047, 1109, 1175, 1245, 
  1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976, 
  2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 
  3322, 3520, 3729, 3951, 4186, 4435, 4699, 4978, 
  5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902, 
  8372, 8870, 9397, 9956, 10548, 11175, 11840, 12544
};

typedef struct beet_t beet_t;
typedef struct part_t part_t;

struct beet_t {
  float freq; // in hertz
  double level; // 0 -> 16383, current temp. volume
  
  float target_freq; // in hertz
  int volume; // 0 -> 16383
  
  int on_speed;  // volume units per 33 ms
  int off_speed; // volume units per 33 ms
  
  int state; // 1 if on
  int type; // 0 if square, 1 if saw, 2 if noise
  
  int stay; // 1 if stay
};

struct part_t {
  int x, y;
};

int16_t *wave_data = NULL;
beet_t channels[48];

part_t parts[128];

FILE *midi = NULL;
int head_pos = 0;

uint8_t *buff_ptr = NULL;
int buff_pos = 0;

int played = 0;
int tick = 0;

FILE *config = NULL;
double rel_time = 0;

void note_on(uint8_t note, uint8_t vol) {
  played = 1;
  
  for (int i = 0; i < 48; i++) {
    if (!channels[i].state && channels[i].level == 0) {
      channels[i].target_freq = freqs[note] / 2.0;
      channels[i].volume = 60 * sqrt(freqs[note]) /* + (vol * 2560) / 100 */;
      
      channels[i].state = 1;
      channels[i].freq = channels[i].target_freq;
      
      return;
    }
  }
  
  printf("(trying to fix disaster)\n");
  fflush(stdout);
  
  int lowest_index = -1;
  int lowest_level = 32767;
  
  for (int i = 0; i < 48; i++) {
    if (!channels[i].state && channels[i].level < lowest_level) {
      lowest_index = i;
      lowest_level = channels[i].level;
    }
  }
  
  if (lowest_index != -1) {
    channels[lowest_index].target_freq = freqs[note] / 2.0;
    channels[lowest_index].volume = (vol * 4096) / 100;
    
    channels[lowest_index].state = 1;
    channels[lowest_index].freq = channels[lowest_index].target_freq;
  }
}

void note_off(uint8_t note) {
  for (int i = 0; i < 48; i++) {
    if (channels[i].target_freq == freqs[note] / 2.0 && channels[i].state) {
      channels[i].state = 0;
      return;
    }
  }
}

uint8_t read_byte(void) {
  uint8_t value = buff_ptr[buff_pos++];
  return value;
}

uint16_t read_word(void) {
  uint16_t hi = read_byte();
  uint16_t lo = read_byte();
  
  return (hi << 8) | lo;
}

uint32_t read_tword(void) {
  uint32_t hi = read_word();
  uint32_t lo = read_byte();
  
  return (hi << 8) | lo;
}

uint32_t read_dword(void) {
  uint32_t hi = read_word();
  uint32_t lo = read_word();
  
  return (hi << 16) | lo;
}

uint32_t read_var(void) {
  uint32_t value = 0;
  
  for (;;) {
    uint8_t byte = read_byte();
    value = (value << 7) | (byte & 0x7F);
    
    if (!(byte & 0x80)) break;
  }
  
  return value;
}

void beet_sleep(double us) {
  while (GetTime() - rel_time < us / 1000000.0);
  rel_time = GetTime();
}

void *parse_midi(void *) {
  // sleep(15);
  
  fseek(midi, 0, SEEK_END);
  uint32_t size = ftell(midi);
  fseek(midi, 0, SEEK_SET);
  
  buff_ptr = malloc(size);
  fread(buff_ptr, 1, size, midi);
  
  fclose(midi);
  buff_pos = 8;
  
  uint16_t type = read_word();
  
  if (type != 1) {
    printf("invalid type\n");
    // exit(1);
  }
  
  uint16_t track_cnt = read_word();
  uint16_t time_div = read_word(); // ticks per quarter note
  
  uint32_t time_len = 500000;
  
  if (time_div & 0x8000) {
    printf("invalid time division\n");
    // exit(1);
  }
  
  buff_pos = 14;
  
  rel_time = GetTime();
  
  for (int i = 0; i < track_cnt; i++) {
    buff_pos += 4;
    
    uint32_t track_len = read_dword();
    uint32_t track_pos = 0;
    
    uint32_t org_pos = buff_pos;
    
    while (track_pos < track_len) {
      uint32_t delta = read_var();
      beet_sleep((delta * time_len) / time_div);
      
      uint8_t data = read_byte();
      
      if (data == 0xFF) {
        uint8_t meta_type = read_byte();
        uint32_t meta_len = read_var();
        
        if (meta_type == 0x51) {
          time_len = read_tword();
        } else {
          buff_pos += meta_len;
          track_pos += meta_len;
        }
      } else if (data >> 4 == 0x08) {
        uint8_t note = read_byte();
        read_byte(); // ignore volume
        
        note_off(note);
      } else if (data >> 4 == 0x09) {
        uint8_t note = read_byte();
        uint8_t vol = read_byte();
        
        if (vol) {
          note_on(note, vol);
        } else {
          note_off(note); // volume 0 means note off
        }
      }
    }
  }
  
  return NULL;
}

int main(int argc, const char **argv) {
  uint32_t size = 2048;
  
  if (argc != 2) {
    printf("beet: invalid/missing arguments\n");
    printf("usage: %s <config>\n", argv[0]);
    
    return 1;
  }
  
  config = fopen(argv[1], "r");
  if (!config) return 1;
  
  InitWindow(1280, 720, "beet");
  Texture texture, background;
  
  char buffer[256];
  
  cfg_find_str(config, "fore_texture", &buffer);
  texture = LoadTexture(buffer);
  
  cfg_find_str(config, "back_texture", &buffer);
  background = LoadTexture(buffer);
  
  int cycles;
  cfg_find_int(config, "fore_cycles", &cycles);
  
  cfg_find_str(config, "midi", &buffer);
  
  midi = fopen(buffer, "rb");
  if (!midi) return 1;
  
  int rain_cnt, rain_over;
  
  cfg_find_int(config, "rain_count", &rain_cnt);
  cfg_find_int(config, "rain_over", &rain_over);
  
  Color color_1, color_2, color_3;
  
  cfg_find_int(config, "rain_color_1_red"  , &(color_1.r));
  cfg_find_int(config, "rain_color_1_green", &(color_1.g));
  cfg_find_int(config, "rain_color_1_blue" , &(color_1.b));
  cfg_find_int(config, "rain_color_1_alpha", &(color_1.a));
  
  cfg_find_int(config, "rain_color_2_red"  , &(color_2.r));
  cfg_find_int(config, "rain_color_2_green", &(color_2.g));
  cfg_find_int(config, "rain_color_2_blue" , &(color_2.b));
  cfg_find_int(config, "rain_color_2_alpha", &(color_2.a));
  
  cfg_find_int(config, "line_color_red"  , &(color_3.r));
  cfg_find_int(config, "line_color_green", &(color_3.g));
  cfg_find_int(config, "line_color_blue" , &(color_3.b));
  cfg_find_int(config, "line_color_alpha", &(color_3.a));
  
  int wave_attack, wave_release, wave_stay;
  char wave_type[64];
  
  cfg_find_int(config, "wave_attack", &wave_attack);
  cfg_find_int(config, "wave_release", &wave_release);
  
  cfg_find_int(config, "wave_stay", &wave_stay);
  cfg_find_str(config, "wave_type", &wave_type);
  
  for (int i = 0; i < 48; i++) {
    channels[i].freq = 1;
    channels[i].level = 0;
    
    channels[i].volume = 0;
    
    channels[i].on_speed = wave_attack;
    channels[i].off_speed = wave_release;
    
    channels[i].state = 0;
    
    if (!strcmp(wave_type, "square")) {
      channels[i].type = 0;
    } else if (!strcmp(wave_type, "saw")) {
      channels[i].type = 1;
    } else if (!strcmp(wave_type, "noise")) {
      channels[i].type = 2;
    } else if (!strcmp(wave_type, "sine")) {
      channels[i].type = 3;
    } else if (!strcmp(wave_type, "piano")) {
      channels[i].type = 4;
    } else if (!strcmp(wave_type, "pulse")) {
      channels[i].type = 5;
    }
    
    channels[i].stay = wave_stay;
  }
  
  for (int i = 0; i < 128; i++) {
    parts[i].x = rand() % (1280 / 4);
    parts[i].y = (rand() % (720 / 4)) - (720 / 4);
  }
  
  InitAudioDevice();
  SetAudioStreamBufferSizeDefault(size);
  
  AudioStream stream = LoadAudioStream(48000, 16, 1);
  wave_data = malloc(sizeof(int16_t) * size);
  
  PlayAudioStream(stream);
  
  pthread_t thread;
  pthread_create(&thread, NULL, parse_midi, NULL);
  
  // struct sched_param param = (struct sched_param){99};
  // pthread_setschedparam(thread, SCHED_FIFO, &param);
  
  while (!WindowShouldClose()) {
    for (int i = 0; i < size; i++) {
      wave_data[i] = 0;
      
      for (int j = 0; j < 48; j++) {
        int length = (int)(24000 / channels[j].freq);
        if (!channels[j].level) continue;
        
        if (channels[j].type == 0) {
          int state = ((i + head_pos) % length) < (length / 2);
          
          int level = (state ? channels[j].level : -channels[j].level);
          wave_data[i] += level;
        } else if (channels[j].type == 1) {
          int level = ((((i + head_pos) % length) * 2 * channels[j].level) / length) - channels[j].level;
          wave_data[i] += level;
        } else if (channels[j].type == 2) {
          int level = (rand() % (int)(2 * channels[j].level)) - channels[j].level;
          wave_data[i] += level;
        } else if (channels[j].type == 3) {
          double value = 2 * PI * channels[j].freq * ((i + head_pos) / 24000.0);
          
          double level = sin(value);
          wave_data[i] += (int)(level * channels[j].level * 4);
        } else if (channels[j].type == 4) {
          double value = 2 * PI * channels[j].freq * ((i + head_pos) / 24000.0);
          
          double level = sin(value) + sin(value * 2) / 2 + sin(value * 3) / 4 + sin(value * 4) / 8 + sin(value * 5) / 16 + sin(value * 6) / 32;
          level += level * level * level;
          
          wave_data[i] += (int)(level * channels[j].level);
        } else if (channels[j].type == 5) {
          int state = ((i + head_pos) % length) < (length / 6);
          
          int level = (state ? channels[j].level : -channels[j].level);
          wave_data[i] += level;
        }
      }
    }
    
    for (int i = 0; i < 48; i++) {
      channels[i].freq = channels[i].target_freq;
      
      if (channels[i].state) {
        channels[i].level += channels[i].on_speed * GetFrameTime();
        
        if (channels[i].level > channels[i].volume) {
          channels[i].level = channels[i].volume;
          
          if (!channels[i].stay) {
            channels[i].state = 0;
          }
        }
      } else {
        channels[i].level -= channels[i].off_speed * GetFrameTime();
        
        if (channels[i].level < 0) {
          channels[i].level = 0;
        }
      }
    }
    
    if (IsAudioStreamProcessed(stream)) {
      UpdateAudioStream(stream, wave_data, size);
      head_pos += size;
    }
    
    BeginDrawing();
    ClearBackground(BLACK);
    
    DrawTexture(background, 0, 0, WHITE);
    
    for (int i = 0; i < 1279; i++) {
      DrawLine(i, 360 - (360 * wave_data[i % size]) / 32767, i + 1, 360 - (360 * wave_data[(i + 1) % size]) / 32767, color_3);
    }
    
    if (played) {
      tick++;
      played = 0;
      
      for (int i = 0; i < rain_cnt; i++) {
        parts[i].x += (rand() % 3) - 1;
        
        if (parts[i].x < 0) parts[i].x += 1280 / 4;
        parts[i].x %= 1280 / 4;
        
        parts[i].y %= 720 / 4;
        parts[i].y++;
      }
    }
    
    if (!rain_over) {
      for (int i = 0; i < rain_cnt; i++) {
        srand(i + tick);
        DrawRectangle(parts[i].x * 4, parts[i].y * 4, 4, 4, (rand() % 2) ? color_1 : color_2);
      }
    }
    
    srand(time(0) + tick);
    
    DrawTextureTiled(texture,
      (Rectangle){(tick % cycles) * (texture.width / cycles), 0, texture.width / cycles, texture.height},
      (Rectangle){((tick * 4) % (1280 + (texture.width * 4) / cycles)) - (texture.width * 4) / cycles, 720 - texture.height * 4, (texture.width * 4) / cycles, texture.height * 4},
    (Vector2){0, 0}, 0, 4, WHITE);
    
    if (rain_over) {
      for (int i = 0; i < rain_cnt; i++) {
        srand(i + tick);
        DrawRectangle(parts[i].x * 4, parts[i].y * 4, 4, 4, (rand() % 2) ? color_1 : color_2);
      }
    }
    
    EndDrawing();
  }
  
  UnloadAudioStream(stream);
  CloseAudioDevice();
  
  CloseWindow();
  return 0;
}
