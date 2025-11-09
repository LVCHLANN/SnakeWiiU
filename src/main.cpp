#include <SDL.h>
#include <vpad/input.h>
#include <whb/proc.h>
#include <coreinit/time.h>

#include <vector>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <string>

// ----- tiny 3x5 bitmap font -----
struct Glyph { uint8_t row[5]; };
static const Glyph G_SPACE{{0,0,0,0,0}}, G_COLON{{0b010,0,0b010,0,0}},
                   G_PLUS {{0b010,0b010,0b111,0b010,0b010}},
                   G_MINUS{{0,0,0b111,0,0}},
                   G_SLASH{{0b001,0b001,0b010,0b100,0b100}};
static const Glyph DIGITS[10] = {
  {{0b111,0b101,0b101,0b101,0b111}},{{0b010,0b110,0b010,0b010,0b111}},
  {{0b111,0b001,0b111,0b100,0b111}},{{0b111,0b001,0b111,0b001,0b111}},
  {{0b101,0b101,0b111,0b001,0b001}},{{0b111,0b100,0b111,0b001,0b111}},
  {{0b111,0b100,0b111,0b101,0b111}},{{0b111,0b001,0b001,0b010,0b010}},
  {{0b111,0b101,0b111,0b101,0b111}},{{0b111,0b101,0b111,0b001,0b111}},
};
static const Glyph LETTERS[26] = {
  {{0b111,0b101,0b111,0b101,0b101}},{{0b110,0b101,0b110,0b101,0b110}},
  {{0b111,0b100,0b100,0b100,0b111}},{{0b110,0b101,0b101,0b101,0b110}},
  {{0b111,0b100,0b110,0b100,0b111}},{{0b111,0b100,0b110,0b100,0b100}},
  {{0b111,0b100,0b101,0b101,0b111}},{{0b101,0b101,0b111,0b101,0b101}},
  {{0b111,0b010,0b010,0b010,0b111}},{{0b001,0b001,0b001,0b101,0b111}},
  {{0b101,0b110,0b100,0b110,0b101}},{{0b100,0b100,0b100,0b100,0b111}},
  {{0b101,0b111,0b111,0b101,0b101}},{{0b101,0b111,0b111,0b111,0b101}},
  {{0b111,0b101,0b101,0b101,0b111}},{{0b111,0b101,0b111,0b100,0b100}},
  {{0b111,0b101,0b101,0b111,0b001}},{{0b111,0b101,0b111,0b110,0b101}},
  {{0b111,0b100,0b111,0b001,0b111}},{{0b111,0b010,0b010,0b010,0b010}},
  {{0b101,0b101,0b101,0b101,0b111}},{{0b101,0b101,0b101,0b101,0b010}},
  {{0b101,0b101,0b111,0b111,0b101}},{{0b101,0b101,0b010,0b101,0b101}},
  {{0b101,0b101,0b010,0b010,0b010}},{{0b111,0b001,0b010,0b100,0b111}},
};
static const Glyph& glyph_for(char c){
  if(c>='0'&&c<='9')return DIGITS[c-'0'];
  if(c>='A'&&c<='Z')return LETTERS[c-'A'];
  switch(c){case ' ':return G_SPACE;case ':':return G_COLON;case '+':return G_PLUS;case '-':return G_MINUS;case '/':return G_SLASH;default:return G_SPACE;}
}
static int draw_char(SDL_Renderer*r,int x,int y,int s,SDL_Color col,char ch){
  const Glyph& g=glyph_for(ch); SDL_SetRenderDrawColor(r,col.r,col.g,col.b,col.a);
  for(int row=0;row<5;++row){ uint8_t bits=g.row[row];
    for(int cx=0;cx<3;++cx) if(bits&(1<<(2-cx))){ SDL_Rect px{x+cx*s,y+row*s,s,s}; SDL_RenderFillRect(r,&px);}}
  return 4*s;
}
static int draw_text(SDL_Renderer*r,int x,int y,int s,SDL_Color col,const std::string&str){
  int pen=x; for(char ch: str){ char up=(char)std::toupper((unsigned char)ch); pen+=draw_char(r,pen,y,s,col,up);} return pen-x;
}

// ----- game state -----
struct Cell{int x,y;};
static const int GRID_W=40, GRID_H=26;
static SDL_Color BG{8,10,14,255}, BORDER{48,54,61,255},
  SNAKE_H{34,197,94,255}, SNAKE_B{22,163,74,255}, FOOD{239,68,68,255},
  TXT{235,239,245,255}, TXT_DIM{170,178,189,255}, OVER_BG{200,30,30,160}, PAUSE_BG{30,30,30,160};

static std::vector<Cell> snake; static int dirx=1, diry=0; static Cell food;
static bool alive=true; static int score=0, high_score=0;

enum class GameState { WAIT_START, RUNNING, PAUSED, GAME_OVER, WAIT_RESUME };
static GameState state = GameState::WAIT_START;

// one-change-per-step latch
static bool dirChangeArmed = true;

static inline uint64_t os_now(){return OSGetTime();}
static bool occupies(const Cell&c){return std::find_if(snake.begin(),snake.end(),[&](const Cell&s){return s.x==c.x&&s.y==c.y;})!=snake.end();}
static void place_food(){ for(;;){ Cell c{std::rand()%GRID_W,std::rand()%GRID_H}; if(!occupies(c)){food=c;return;}}}
static void new_game(){
  snake.clear(); int cx=GRID_W/2,cy=GRID_H/2;
  snake.push_back({cx,cy}); snake.push_back({cx-1,cy}); snake.push_back({cx-2,cy});
  dirx=1; diry=0; score=0; alive=true; state = GameState::WAIT_START; dirChangeArmed=true; place_food();
}
static void step_logic(){
  if(state!=GameState::RUNNING || !alive) return;
  Cell head=snake.front(); head.x+=dirx; head.y+=diry;
  if(head.x<0||head.x>=GRID_W||head.y<0||head.y>=GRID_H){alive=false; state=GameState::GAME_OVER; return;}
  if(occupies(head)){alive=false; state=GameState::GAME_OVER; return;}
  snake.insert(snake.begin(),head);
  if(head.x==food.x&&head.y==food.y){ score+=1; if(score>high_score)high_score=score; place_food(); }
  else snake.pop_back();
  // allow a new direction change once we made a movement step
  dirChangeArmed = true;
}

// ----- render helpers -----
static void clear(SDL_Renderer*r,const SDL_Color&c){SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a); SDL_RenderClear(r);}
static void fill_rect(SDL_Renderer*r,const SDL_Color&c,int x,int y,int w,int h){ SDL_Rect rc{x,y,w,h}; SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a); SDL_RenderFillRect(r,&rc);}
static void draw_centered(SDL_Renderer*r,int cx,int y,int scale,SDL_Color col,const std::string&text){
  int w=(int)text.size()*4*scale; draw_text(r, cx - w/2, y, scale, col, text);
}

// Recreate renderer safely (used after resume if needed)
static void reset_renderer(SDL_Window*&win, SDL_Renderer*&ren){
  if(ren){ SDL_DestroyRenderer(ren); ren=nullptr; }
  ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  SDL_RenderSetLogicalSize(ren, 854, 480);
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
}

// Build dynamic footer string based on state
static std::string footer_for(GameState s){
  switch(s){
    case GameState::WAIT_START:  return "MOVE: D-PAD / LEFT STICK TO START  \xC2\xB7  HOME: HOME MENU";
    case GameState::WAIT_RESUME: return "MOVE: D-PAD / LEFT STICK TO RESUME  \xC2\xB7  HOME: HOME MENU";
    case GameState::RUNNING:     return "MOVE: D-PAD / LEFT STICK  \xC2\xB7  PAUSE: +  \xC2\xB7  HOME: HOME MENU";
    case GameState::PAUSED:      return "CONTINUE: +  \xC2\xB7  HOME: HOME MENU";
    case GameState::GAME_OVER:   return "RESTART: A  \xC2\xB7  HOME: HOME MENU";
  }
  return "";
}

// Return true if exactly ONE cardinal direction is pressed (dpad or stick)
// and write it to outdx/outdy. Otherwise return false.
static bool single_direction(const VPADStatus& v, int& outdx, int& outdy){
  const float t = 0.55f;
  bool l = (v.leftStick.x <= -t) || (v.hold & VPAD_BUTTON_LEFT)  || (v.trigger & VPAD_BUTTON_LEFT);
  bool r = (v.leftStick.x >=  t) || (v.hold & VPAD_BUTTON_RIGHT) || (v.trigger & VPAD_BUTTON_RIGHT);
  bool u = (-v.leftStick.y <= -t) || (v.hold & VPAD_BUTTON_UP)    || (v.trigger & VPAD_BUTTON_UP);   // invert Y
  bool d = (-v.leftStick.y >=  t) || (v.hold & VPAD_BUTTON_DOWN)  || (v.trigger & VPAD_BUTTON_DOWN);

  int count = (int)l + (int)r + (int)u + (int)d;
  if(count != 1) return false;

  if(l){ outdx=-1; outdy=0;  return true; }
  if(r){ outdx= 1; outdy=0;  return true; }
  if(u){ outdx=0;  outdy=-1; return true; }
  if(d){ outdx=0;  outdy= 1; return true; }
  return false;
}
static inline bool is_opposite(int ax,int ay,int bx,int by){ return (ax == -bx) && (ay == -by); }

int main(int, char**){
  WHBProcInit();

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"nearest");
  if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)!=0){
    WHBProcShutdown();
    return 0;
  }

  SDL_Window* win=nullptr; SDL_Renderer* ren=nullptr;
  SDL_CreateWindowAndRenderer(1280,720,SDL_WINDOW_SHOWN,&win,&ren);
  SDL_RenderSetLogicalSize(ren,854,480);
  SDL_SetRenderDrawBlendMode(ren,SDL_BLENDMODE_BLEND);

  VPADInit(); std::srand((unsigned)os_now()); new_game();

  const int LSW=854, LSH=480, margin=20;
  const int boxX=margin, boxY=margin+20, boxW=LSW-margin*2, boxH=LSH-margin*2-40;
  const float cellW=(float)boxW/GRID_W, cellH=(float)boxH/GRID_H;
  const int SCALE_SMALL=2, SCALE_MED=3, SCALE_BIG=6;

  uint64_t last=os_now(); double accumMs=0.0; const double baseStepMs=140.0, minStepMs=70.0;
  bool requestExit=false; int warmupPresents=0; bool needRendererReset=false;

  while(WHBProcIsRunning() && !requestExit){
    // Close Software path
    SDL_Event ev;
    while(SDL_PollEvent(&ev)){
      if(ev.type==SDL_QUIT){ requestExit=true; }
    }

    // ----- Input (ignore HOME) -----
    VPADStatus v{}; VPADReadError err{}; VPADRead(VPAD_CHAN_0,&v,1u,&err);
    if(err==VPAD_READ_SUCCESS){
      // Pause toggle on PLUS
      if(v.trigger & VPAD_BUTTON_PLUS){
        if(state == GameState::RUNNING){ state = GameState::PAUSED; }
        else if(state == GameState::PAUSED){ state = GameState::RUNNING; }
      }

      int ndx=0, ndy=0;
      bool hasSingle = single_direction(v, ndx, ndy);

      if(state == GameState::WAIT_START || state == GameState::WAIT_RESUME){
        // Start/resume only on a single, non-opposite direction (or same dir).
        if(hasSingle && !is_opposite(ndx,ndy,dirx,diry)){
          // If they pressed a perpendicular or same direction, accept.
          if(!(ndx==dirx && ndy==diry)){
            dirx = ndx; diry = ndy;
          }
          state = GameState::RUNNING;
          dirChangeArmed = false; // already used this frame; arm after next step
        }
      } else if(state == GameState::RUNNING){
        // One change per movement step; disallow 180° reversal
        if(hasSingle && dirChangeArmed && !is_opposite(ndx,ndy,dirx,diry) && !(ndx==dirx && ndy==diry)){
          dirx = ndx; diry = ndy;
          dirChangeArmed = false;
        }
      } else if(state == GameState::GAME_OVER){
        if(v.trigger & VPAD_BUTTON_A){
          new_game();
        }
      }
    }

    // ----- Time / resume detection -----
    uint64_t now=os_now();
    double dtMs = OSTicksToMilliseconds(now - last);
    last = now;

    if(dtMs > 500.0){
      // Coming back from HOME: gate on resume and warm-up
      accumMs = 0.0;
      warmupPresents = 2;
      needRendererReset = true;
      if(state != GameState::GAME_OVER && state != GameState::PAUSED)
        state = GameState::WAIT_RESUME;
    }else if(state == GameState::RUNNING){
      accumMs += dtMs;
      double stepMs=baseStepMs - std::max(0,(int)snake.size()-3)*2.0; if(stepMs<minStepMs) stepMs=minStepMs;
      while(accumMs>=stepMs){ accumMs-=stepMs; step_logic(); }
    }

    // ----- Render -----
    clear(ren,BG);

    // HUD
    char buf[64]; std::snprintf(buf,sizeof(buf),"SCORE:%d  HIGH:%d",score,high_score);
    draw_text(ren, margin, margin-14, SCALE_MED, TXT, buf);

    // Dynamic footer
    std::string footer = footer_for(state);
    draw_text(ren, margin, (LSH - margin + 4), SCALE_SMALL, TXT_DIM, footer);

    // Border
    auto line=[&](int x,int y,int w,int h,const SDL_Color&c){ fill_rect(ren,c,x,y,w,h); };
    line(boxX-2,boxY-2,boxW+4,2,BORDER);
    line(boxX-2,boxY+boxH,boxW+4,2,BORDER);
    line(boxX-2,boxY-2,2,boxH+4,BORDER);
    line(boxX+boxW,boxY-2,2,boxH+4,BORDER);

    // Food
    { int x=boxX+(int)(food.x*cellW), y=boxY+(int)(food.y*cellH);
      int w=std::max(1,(int)cellW-2), h=std::max(1,(int)cellH-2);
      fill_rect(ren,FOOD,x+1,y+1,w,h);
    }

    // Snake
    for(size_t i=0;i<snake.size();++i){
      const Cell&s=snake[i];
      int x=boxX+(int)(s.x*cellW), y=boxY+(int)(s.y*cellH);
      int w=std::max(1,(int)cellW-2), h=std::max(1,(int)cellH-2);
      fill_rect(ren,(i==0?SNAKE_H:SNAKE_B),x+1,y+1,w,h);
    }

    // Faint grid
    SDL_SetRenderDrawColor(ren,255,255,255,18);
    for(int gx=1;gx<GRID_W;++gx){ int x=boxX+(int)(gx*cellW); SDL_RenderDrawLine(ren,x,boxY,x,boxY+boxH); }
    for(int gy=1;gy<GRID_H;++gy){ int y=boxY+(int)(gy*cellH); SDL_RenderDrawLine(ren,boxX,y,boxX+boxW,y); }

    // Overlays
    int cx = boxX + boxW/2;
    if(state == GameState::WAIT_START){
      draw_centered(ren, cx, boxY + boxH/2 - 20, 4, TXT, "PRESS D-PAD OR LEFT STICK TO START");
    } else if(state == GameState::WAIT_RESUME){
      draw_centered(ren, cx, boxY + boxH/2 - 20, 4, TXT, "PRESS D-PAD OR LEFT STICK TO RESUME");
    } else if(state == GameState::PAUSED){
      fill_rect(ren, PAUSE_BG, boxX, boxY, boxW, boxH);
      draw_centered(ren, cx, boxY + boxH/2 - 20, 6, TXT, "PAUSED");
      draw_centered(ren, cx, boxY + boxH/2 + 24, 3, TXT_DIM, "PRESS + TO CONTINUE  |  HOME: HOME MENU");
    } else if(state == GameState::GAME_OVER){
      fill_rect(ren, OVER_BG, boxX, boxY, boxW, boxH);
      draw_centered(ren, cx, boxY + boxH/2 - 20, 6, TXT, "GAME OVER");
      draw_centered(ren, cx, boxY + boxH/2 + 24, 3, TXT_DIM, "PRESS A TO RESTART  |  HOME: HOME MENU");
    }

    SDL_RenderPresent(ren);

    // Warm-up after resume (extra presents + optional renderer reset)
    if(warmupPresents > 0){
      if(needRendererReset){
        reset_renderer(win, ren);
        needRendererReset = false;
        clear(ren,BG);
        SDL_RenderPresent(ren);
      }
      SDL_RenderPresent(ren);
      warmupPresents = 0;
    }

    SDL_Delay(1);
  }

  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  VPADShutdown();
  WHBProcShutdown();
  return 0;
}
