#include "ui_pages.h"
#include "ui_canvas.h"
#include <stdio.h>

enum {
    PAPER=0xF8F5EB, INK=0x203D31, QUIET=0x5C7063, GREEN=0x205D45,
    TILE=0xFFFDF6, SOFT=0xE4EBDB, BORDER=0xD3DDCA, ORANGE=0xB64D25,
    WHITE=0xFFFFFF, LEAF=0xA8BD6A, LIGHT_LEAF=0xC1CE83,
};

static void centered(int y, const char *text, int size, unsigned color)
{
    ui_text((320-ui_text_width(text,size))/2,y,text,size,color);
}

static void arrow(int x,int y,unsigned color)
{
    ui_line(x,y+6,x+14,y+6,2,color);
    ui_line(x+8,y,x+14,y+6,2,color);
    ui_line(x+8,y+12,x+14,y+6,2,color);
}

static void camera_icon(int x,int y,int size,unsigned color,unsigned bg)
{
    ui_rect(x,y+size/5,size,size*3/5,4,color);
    ui_rect(x+2,y+size/5+2,size-4,size*3/5-4,2,bg);
    ui_rect(x+size/3,y+size/10,size/3,size/5,2,color);
    ui_ellipse(x+size/3,y+size/3,size/3,size/3,color);
    ui_ellipse(x+size/3+2,y+size/3+2,size/3-4,size/3-4,bg);
}

static void book_icon(int x,int y,unsigned color,unsigned bg)
{
    ui_rect(x,y,30,25,4,color);
    ui_rect(x+2,y+2,26,21,2,bg);
    ui_line(x+15,y+1,x+15,y+24,2,color);
    ui_line(x+5,y+7,x+11,y+7,2,color);
    ui_line(x+19,y+7,x+25,y+7,2,color);
    ui_line(x+5,y+12,x+11,y+12,2,color);
}

static void map_icon(int x,int y,unsigned color)
{
    ui_line(x,y+4,x+9,y,2,color);ui_line(x+9,y,x+20,y+4,2,color);
    ui_line(x+20,y+4,x+29,y,2,color);ui_line(x,y+4,x,y+27,2,color);
    ui_line(x+9,y,x+9,y+23,2,color);ui_line(x+20,y+4,x+20,y+27,2,color);
    ui_line(x+29,y,x+29,y+23,2,color);ui_line(x,y+27,x+9,y+23,2,color);
    ui_line(x+9,y+23,x+20,y+27,2,color);ui_line(x+20,y+27,x+29,y+23,2,color);
}

static void battle_icon(int x,int y,unsigned color)
{
    ui_line(x+3,y+2,x+26,y+25,3,color);ui_line(x+26,y+2,x+3,y+25,3,color);
    ui_line(x+2,y+18,x+10,y+26,3,color);ui_line(x+20,y+26,x+28,y+18,3,color);
    ui_line(x+3,y+2,x+10,y+3,2,color);ui_line(x+26,y+2,x+19,y+3,2,color);
}

static void plant(int x,int y,int scale)
{
    ui_line(x+scale/2,y+scale/3,x+scale/2+8,y+scale,3,0x789554);
    ui_ellipse(x+scale/8,y+scale/10,scale*2/5,scale*2/5,0x8EA95C);
    ui_ellipse(x+scale/2,y+scale/7,scale*2/5,scale*2/5,LIGHT_LEAF);
    ui_ellipse(x+scale/3,y+scale*2/5,scale*2/5,scale*2/5,LEAF);
    ui_line(x+scale/2,y+scale/2,x+scale/3,y+scale/3,1,0xD5DFA0);
}

static void status(void)
{
    ui_text(14,5,"界面预览",13,QUIET);
    ui_text(247,5,"USB供电",13,QUIET);
}

static void header(const char *title)
{
    ui_line(28,43,19,52,2,GREEN);ui_line(19,52,28,61,2,GREEN);
    ui_text(52,37,title,20,INK);
    ui_line(14,76,306,76,1,BORDER);
}

static void button(int y,const char *label,unsigned color)
{
    ui_rect(14,y,292,48,16,color);
    centered(y+11,label,18,color==GREEN||color==ORANGE?WHITE:GREEN);
}

static void home(bool calibrated)
{
    ui_text(16,37,"去发现身边的自然",13,GREEN);
    ui_text(16,61,"小小探索家",26,INK);
    // Settings is a labeled, generous hit target rather than a tiny gear.
    ui_rect(260,39,48,48,14,SOFT);
    ui_text(268,53,"设置",16,GREEN);
    ui_rect(14,110,292,154,20,GREEN);
    ui_text(32,128,"今天，发现什么？",24,WHITE);
    ui_text(33,165,"把好奇心装进口袋",13,0xE4EBDB);
    plant(211,163,85);
    camera_icon(33,224,22,WHITE,GREEN);
    ui_text(64,219,"拍照发现",18,WHITE);arrow(154,226,WHITE);
    const char *labels[]={"探索地图","我的图鉴","伙伴对战"};
    for(int i=0;i<3;i++){
        int x=14+i*100;
        ui_rect(x,276,92,100,16,BORDER);
        ui_rect(x+1,277,90,98,15,TILE);
        if(i==0)map_icon(x+31,294,GREEN);
        if(i==1)book_icon(x+31,294,GREEN,TILE);
        if(i==2)battle_icon(x+31,294,GREEN);
        ui_text(x+(92-ui_text_width(labels[i],16))/2,340,labels[i],16,INK);
    }
    ui_line(16,391,304,391,1,BORDER);
    ui_text(17,405,calibrated?"让每次发现，都有迹可循":"轻点屏幕，完成首次触摸校准",16,INK);
    ui_text(17,434,calibrated?"按住任意位置3秒，可重新校准":"校准一次，下次开机直接使用",13,QUIET);
}

static void camera_page(void)
{
    header("拍照发现");
    ui_rect(14,88,292,247,18,SOFT);
    ui_rect(28,101,130,26,8,TILE);ui_text(38,104,"摄像头待接入",16,QUIET);
    plant(97,151,130);
    const int x0=49,x1=270,y0=144,y1=302;
    ui_line(x0,y0,x0+22,y0,2,WHITE);ui_line(x0,y0,x0,y0+22,2,WHITE);
    ui_line(x1,y0,x1-22,y0,2,WHITE);ui_line(x1,y0,x1,y0+22,2,WHITE);
    ui_line(x0,y1,x0+22,y1,2,WHITE);ui_line(x0,y1,x0,y1-22,2,WHITE);
    ui_line(x1,y1,x1-22,y1,2,WHITE);ui_line(x1,y1,x1,y1-22,2,WHITE);
    centered(350,"先留住好奇，再认识它",18,INK);
    centered(382,"这里将显示摄像头画面",13,QUIET);
    button(418,"看看我的图鉴",GREEN);
}

static void map_page(void)
{
    header("探索地图");
    ui_rect(14,88,292,244,18,SOFT);
    // A map symbol only: never show a fabricated current position or track.
    map_icon(146,145,GREEN);
    centered(201,"每一步，都是新发现",20,INK);
    centered(241,"地图与定位尚未接入",16,QUIET);
    centered(349,"以后在这里留下你的足迹",16,INK);
    centered(382,"当前位置：尚未获取",13,QUIET);
    button(418,"去拍照发现",GREEN);
}

static void collection_page(void)
{
    header("我的图鉴");
    ui_rect(14,89,292,45,12,SOFT);ui_text(28,101,"已收藏 · 0",16,GREEN);
    ui_ellipse(116,164,88,88,SOFT);book_icon(145,195,GREEN,SOFT);
    centered(275,"第一位自然朋友",24,INK);
    centered(318,"图鉴从第一张照片开始",16,QUIET);
    centered(348,"未来，每次发现都能留在这里",13,QUIET);
    button(418,"去拍照发现",ORANGE);
}

static void battle_page(void)
{
    header("伙伴对战");
    ui_rect(14,89,292,211,18,TILE);
    ui_text(30,107,"和朋友",24,INK);ui_text(30,142,"来一场友谊赛",24,INK);
    ui_rect(238,111,49,49,14,SOFT);battle_icon(248,121,GREEN);
    ui_text(30,204,"各选一张收集的卡",16,QUIET);
    ui_text(30,236,"一起认识身边的自然",16,QUIET);
    centered(323,"伙伴连接尚未接入",16,QUIET);
    centered(360,"先去认识你的第一位自然朋友",13,QUIET);
    button(418,"看看我的图鉴",GREEN);
}

static void settings(void)
{
    header("设置");
    ui_text(22,100,"重新校准",20,INK);ui_text(22,131,"重新设置点击位置",13,QUIET);
    arrow(278,113,GREEN);ui_line(18,162,302,162,1,BORDER);
    ui_text(22,180,"界面说明",20,INK);ui_text(22,211,"当前功能与操作方式",13,QUIET);
    arrow(278,193,GREEN);ui_line(18,242,302,242,1,BORDER);
    ui_rect(14,270,292,104,16,SOFT);
    ui_text(29,285,"自然手帐",20,GREEN);
    ui_text(29,320,"320 × 480 · 竖屏",16,QUIET);
    button(418,"回到首页",GREEN);
}

static void about(void)
{
    header("界面说明");
    ui_text(20,101,"这一版可以做什么",20,INK);
    ui_text(20,145,"首页、四个入口与返回",16,QUIET);
    ui_text(20,180,"首次触摸校准与保存",16,QUIET);
    ui_line(20,220,300,220,1,BORDER);
    ui_text(20,245,"更多发现，正在准备",20,INK);
    ui_text(20,287,"拍照、地图、图鉴保存和对战",16,QUIET);
    ui_text(20,321,"将在后续接入真实功能",16,QUIET);
    button(418,"回到首页",GREEN);
}

void ui_pages_draw(ui_page_t page,bool calibrated)
{
    ui_canvas_reset(PAPER);status();
    switch(page){
        case UI_HOME:home(calibrated);break;
        case UI_CAMERA:camera_page();break;
        case UI_MAP:map_page();break;
        case UI_COLLECTION:collection_page();break;
        case UI_BATTLE:battle_page();break;
        case UI_SETTINGS:settings();break;
        case UI_ABOUT:about();break;
        default:home(calibrated);break;
    }
}

ui_page_t ui_pages_tap(ui_page_t page,int x,int y)
{
    if(page==UI_HOME){
        if(x>=260&&y>=30&&y<100)return UI_SETTINGS;
        if(x>=14&&x<306&&y>=110&&y<264)return UI_CAMERA;
        if(y>=276&&y<376){
            if(x>=14&&x<106)return UI_MAP;
            if(x>=114&&x<206)return UI_COLLECTION;
            if(x>=214&&x<306)return UI_BATTLE;
        }
    }else{
        if(x<56&&y>=28&&y<77)return UI_HOME;
        if(page==UI_SETTINGS&&x>=14&&x<306){
            if(y>=85&&y<162)return UI_CALIBRATE;
            if(y>=166&&y<243)return UI_ABOUT;
        }
        if(x>=14&&x<306&&y>=418&&y<466){
            if(page==UI_CAMERA||page==UI_BATTLE)return UI_COLLECTION;
            if(page==UI_MAP||page==UI_COLLECTION)return UI_CAMERA;
            return UI_HOME;
        }
    }
    return page;
}

const char *ui_page_name(ui_page_t page)
{
    const char *names[]={"home","camera","map","collection","battle","settings","about","calibrate"};
    return page>=UI_HOME&&page<=UI_CALIBRATE?names[page]:"invalid";
}

void ui_calibration_draw(int step,bool failed)
{
    const int target_x[]={36,284,284,36},target_y[]={96,96,376,376};
    char count[24];snprintf(count,sizeof(count),"%d / 4",step+1);
    ui_canvas_reset(PAPER);status();
    centered(38,"触摸校准",24,INK);
    centered(151,count,24,GREEN);
    centered(207,"按住十字中心，再松开",20,INK);
    centered(246,"请用触控笔或指尖轻按",16,QUIET);
    centered(285,failed?"刚才没对准，请再试一次":"四个角完成后会自动保存",13,failed?ORANGE:QUIET);
    const int x=target_x[step],y=target_y[step];
    ui_ellipse(x-18,y-18,36,36,GREEN);ui_ellipse(x-15,y-15,30,30,PAPER);
    ui_line(x-24,y,x+24,y,2,GREEN);ui_line(x,y-24,x,y+24,2,GREEN);
    ui_ellipse(x-3,y-3,6,6,ORANGE);
    centered(433,"按住任意位置3秒，可重新开始",13,QUIET);
}
