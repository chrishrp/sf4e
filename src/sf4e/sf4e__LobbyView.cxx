#include "sf4e__LobbyView.hxx"
#include "sf4e__LobbyCatalog.hxx"
#include "sf4e__LobbyPortraits.hxx"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace sf4e { namespace LobbyView { namespace {
const ImU32 ink=IM_COL32(13,14,18,255), paper=IM_COL32(246,235,211,255);
const ImU32 muted=IM_COL32(181,178,171,255), gold=IM_COL32(255,195,71,255);
const ImU32 red=IM_COL32(224,51,50,255), blue=IM_COL32(72,162,247,255);
const ImU32 green=IM_COL32(132,219,129,255), edge=IM_COL32(110,106,99,170);
ImU32 UltraColor(int ultra) { auto c=LobbyCatalog::UltraColor(ultra); return IM_COL32(c.r,c.g,c.b,255); }
void Text(ImDrawList* dl, ImFont* font, float size, float x,float y, ImU32 color,const char* text,float width=10000) {
    float w=font->CalcTextSizeA(size,10000,0,text).x;
    if(w>width) size*=width/w;
    dl->AddText(font,size,ImVec2(x,y),color,text);
}
void Center(ImDrawList* dl,ImFont* font,float size,float x,float y,ImU32 color,const char* text) {
    Text(dl,font,size,x-font->CalcTextSizeA(size,10000,0,text).x/2,y,color,text);
}
bool Contains(ImVec2 p,ImVec2 a,ImVec2 b) {return p.x>=a.x&&p.x<=b.x&&p.y>=a.y&&p.y<=b.y;}
void Frame(ImDrawList* dl,ImVec2 a,ImVec2 b,bool focused,ImU32 accent=gold) {
    dl->AddRectFilled(a,b,IM_COL32(19,21,27,244));
    dl->AddRect(a,b,focused?accent:edge,0,0,focused?3.f:1.f);
}
const char* Name(int id) {auto f=LobbyCatalog::Find(id);return f?f->name:"Selecting...";}
void Portrait(ImDrawList* dl,int id,ImVec2 a,ImVec2 b,bool rosterIcon=false) {
    dl->AddRectFilled(a,b,IM_COL32(30,33,42,255));
    if(!LobbyPortraits::Draw(dl,id,a,b,rosterIcon)) {
        dl->AddRectFilled(a,b,IM_COL32(30,33,42,255));
        ImVec2 c((a.x+b.x)/2,(a.y+b.y)/2);
        dl->AddCircleFilled(ImVec2(c.x,c.y-18),22,edge);
        dl->AddTriangleFilled(ImVec2(c.x-42,b.y-8),ImVec2(c.x,b.y-62),ImVec2(c.x+42,b.y-8),edge);
    }
}
void Arrow(ImDrawList* dl,ImVec2 c,float dx,float dy) {
    float n=std::sqrt(dx*dx+dy*dy);dx/=n;dy/=n;
    ImVec2 a(c.x-dx*7,c.y-dy*7), b(c.x+dx*7,c.y+dy*7);
    dl->AddLine(a,b,paper,2);
    dl->AddTriangleFilled(b,ImVec2(b.x-dx*6-dy*4,b.y-dy*6+dx*4),ImVec2(b.x-dx*6+dy*4,b.y-dy*6-dx*4),paper);
}
void Command(ImDrawList* dl,const Fonts& f,const char* input,float x,float y,float width,ImU32 accent) {
    std::istringstream stream(input);std::string token;float start=x;
    while(stream>>token) {
        float dx=0,dy=0;
        bool direction=true;
        if(token=="D")dy=1;else if(token=="U")dy=-1;else if(token=="F")dx=1;else if(token=="B")dx=-1;
        else if(token=="DF"){dx=1;dy=1;}else if(token=="DB"){dx=-1;dy=1;}
        else if(token=="UF"){dx=1;dy=-1;}else if(token=="UB"){dx=-1;dy=-1;}else direction=false;
        if(token=="CHARGE")token="Charge";else if(token=="AIR")token="Air";
        else if(token=="NEAR")token="Near";else if(token=="OR")token="or";
        float w=direction?22.f:(std::max)(16.f,f.caption->CalcTextSizeA(16,1000,0,token.c_str()).x+12);
        if(x+w>start+width){x=start;y+=30;}
        if(direction){dl->AddCircle(ImVec2(x+12,y+12),11,edge);Arrow(dl,ImVec2(x+12,y+12),dx,dy);}
        else if(token=="+")Text(dl,f.caption,18,x+2,y+2,muted,"+");
        else {dl->AddRectFilled(ImVec2(x,y),ImVec2(x+w-2,y+24),accent,3);Text(dl,f.caption,16,x+5,y+2,ink,token.c_str());}
        x+=w+2;
    }
}
void PlayerCard(ImDrawList* dl,const Fonts& f,const Player& p,int side,bool scores,float x) {
    float y=128,w=662,h=218;ImU32 accent=side==0?red:blue;
    Frame(dl,ImVec2(x,y),ImVec2(x+w,y+h),p.local,accent);
    Portrait(dl,p.character,ImVec2(x+1,y+1),ImVec2(x+216,y+h-1));
    dl->AddRectFilledMultiColor(ImVec2(x+125,y+1),ImVec2(x+218,y+h-1),0,IM_COL32(19,21,27,255),IM_COL32(19,21,27,255),0);
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+6,y+h),accent);
    Text(dl,f.caption,17,x+234,y+15,accent,side==0?"P1":"P2");
    Text(dl,f.caption,15,x+w-86,y+15,p.local?gold:muted,p.local?"You":"");
    Text(dl,f.body,25,x+234,y+40,paper,p.name.c_str(),w-252);
    Text(dl,f.head,42,x+234,y+77,paper,Name(p.character),w-252);
    char tally[80];
    if(scores&&p.present)std::snprintf(tally,sizeof(tally),"%llu W   /   %llu L",p.wins,p.losses);
    else std::snprintf(tally,sizeof(tally),"%s",p.present?"Score unavailable":"Open seat");
    Text(dl,f.body,21,x+234,y+131,scores?gold:muted,tally,w-252);
    if(p.character>=0){ImU32 uc=UltraColor(p.ultra);Text(dl,f.caption,17,x+234,y+174,uc,p.ultra==0?"Ultra I":p.ultra==1?"Ultra II":"Ultra Double");}
    Text(dl,f.caption,17,x+w-137,y+174,p.ready?green:muted,p.ready?"Ready":"Selecting",120);
}
} // namespace

Hit Draw(ImDrawList* dl,const Fonts& f,const Model& m,ImVec2 mouse,bool click) {
    Hit hit;
    dl->AddRectFilled(ImVec2(0,0),ImVec2(1600,1000),ink);
    dl->AddQuadFilled(ImVec2(0,0),ImVec2(890,0),ImVec2(606,390),ImVec2(0,522),IM_COL32(84,26,29,255));
    dl->AddQuadFilled(ImVec2(1600,0),ImVec2(1240,0),ImVec2(891,467),ImVec2(1600,231),IM_COL32(26,47,65,255));
    for(int i=0;i<21;++i)dl->AddLine(ImVec2(24.f+i*80,0),ImVec2(-560.f+i*80,1000),IM_COL32(230,215,181,7),2);
    Text(dl,f.title,58,54,47,paper,m.spectator?"Spectating":"Character select");
    Text(dl,f.caption,16,1190,25,muted,m.publicRoom?"Public room":"Private room");
    Text(dl,f.head,46,1188,45,gold,m.code.empty()?"------":m.code.c_str(),255);
    PlayerCard(dl,f,m.players[0],0,m.scoresAvailable,56);
    PlayerCard(dl,f,m.players[1],1,m.scoresAvailable,882);
    Center(dl,f.head,64,800,172,paper,"VS");
    if(m.scoresAvailable){char score[64];std::snprintf(score,sizeof(score),"%llu : %llu",m.players[0].wins,m.players[1].wins);Center(dl,f.head,30,800,274,gold,score);}
    else Center(dl,f.caption,17,800,278,muted,"-- : --");
    if(!m.watchers.empty())Text(dl,f.caption,16,56,365,muted,("Spectators: "+m.watchers).c_str(),994);
    auto target=[&](ImVec2 a,ImVec2 b,int row,int index){if(!m.ready&&!m.spectator&&Contains(mouse,a,b)){hit.row=row;hit.index=index;hit.activate=click;}};
    const float cellW=86.36f,cellH=88.f;
    for(int i=0;i<LobbyCatalog::CharacterCount;++i){
        ImVec2 a(56+(i%11)*(cellW+6),400+(i/11)*(cellH+6)),b(a.x+cellW,a.y+cellH);
        bool selected=i==m.character,focused=m.focusRow==0&&m.characterCursor==i&&!m.spectator;
        Portrait(dl,i,a,b,true);
        dl->AddRectFilledMultiColor(ImVec2(a.x,b.y-27),b,IM_COL32(0,0,0,75),IM_COL32(0,0,0,75),IM_COL32(0,0,0,245),IM_COL32(0,0,0,245));
        Text(dl,f.caption,13,a.x+4,b.y-19,paper,Name(i),cellW-8);
        dl->AddRect(a,b,focused?paper:selected?gold:edge,0,0,focused?3.f:selected?3.f:1.f);
        if(selected)dl->AddRectFilled(a,ImVec2(a.x+cellW,a.y+4),gold);
        target(a,b,0,i);
    }
    // Stage is always visible, with the actual shared stage separate from P2's proposal.
    const float rx=1110,rw=434;
    Frame(dl,ImVec2(rx,400),ImVec2(1544,532),m.focusRow==1&&m.optionCursor==4);
    Text(dl,f.caption,17,rx+18,414,gold,m.side==0?"Stage":"Stage / P1");
    auto stage=LobbyCatalog::FindStage(m.stage);
    Text(dl,f.head,32,rx+18,457,paper,stage?stage->name:"Waiting for P1",rw-36);
    const auto proposal=LobbyCatalog::FindStage(m.proposedStage);
    std::string stageOption=m.side==1&&m.proposedStage!=m.stage&&proposal?
        std::string("Next: ")+proposal->name:"Stage";
    target(ImVec2(rx,400),ImVec2(1544,532),1,4);
    Frame(dl,ImVec2(rx,548),ImVec2(1544,836),m.focusRow==1&&m.optionCursor==2,UltraColor(m.ultra));
    const char* ultraLabel=m.editionId==16?"Omega (unverified)":m.ultra==2?"Ultra Double":"Ultra";
    Text(dl,f.caption,17,rx+18,560,UltraColor(m.ultra),ultraLabel,rw-36);
    auto fighter=LobbyCatalog::Find(m.character);
    if(!fighter)Text(dl,f.body,22,rx+18,622,muted,"Waiting for P1",rw-36);
    if(fighter)for(int u=0;u<2;++u){
        float y=590.f+u*103;
        const auto* move=LobbyCatalog::FindUltra(m.character,u,m.editionId);
        if(!move&&m.editionId==16)move=&fighter->ultra[u];
        if(!move){Text(dl,f.caption,17,rx+18,y,muted,"Ultra II unavailable",rw-36);continue;}
        bool picked=m.ultra==u||m.ultra==2;ImU32 accent=UltraColor(u);
        if(picked)dl->AddRectFilled(ImVec2(rx+2,y),ImVec2(rx+6,y+88),accent);
        Text(dl,f.head,21,rx+18,y,accent,u==0?"I":"II");
        Text(dl,f.body,20,rx+47,y,accent,move->name,rw-68);
        Command(dl,f,move->input,rx+18,y+28,rw-36,accent);
    }
    target(ImVec2(rx,548),ImVec2(1544,836),1,2);
    char costume[30],color[30];std::snprintf(costume,sizeof(costume),"Costume %d",m.costume+1);std::snprintf(color,sizeof(color),"Color %d",m.color+1);
    const char* opts[]={costume,color,m.ultra==0?"Ultra I":m.ultra==1?"Ultra II":"Ultra Double",m.edition.c_str(),stageOption.c_str()};
    if(!m.spectator||fighter)for(int i=0;i<5;++i){float x=56+i*207;ImVec2 a(x,790),b(x+195,836);bool focus=!m.spectator&&m.focusRow==1&&m.optionCursor==i;
        Frame(dl,a,b,focus,i==2?UltraColor(m.ultra):gold);Text(dl,f.body,19,x+12,803,focus?paper:muted,opts[i],171);target(a,b,1,i);}
    if(m.spectator){Text(dl,f.head,30,56,869,paper,m.spectatorStatus.c_str(),1488);}
    else {
        const char* actions[]={m.ready?"Waiting":"Ready","Leave room","Random fighter","Random Ultra","Random stage"};
        for(int i=0;i<5;++i){float x=56+i*301;ImVec2 a(x,872),b(x+284,924);bool focus=m.focusRow==2&&m.actionCursor==i;
            ImU32 accent=i==0?green:i==1?red:gold;Frame(dl,a,b,focus,accent);
            if(i==0)dl->AddRectFilled(a,b,m.ready?IM_COL32(50,77,46,255):IM_COL32(166,36,38,255));
            Text(dl,f.head,25,x+16,884,i==0?paper:focus?accent:paper,actions[i],253);target(a,b,2,i);}
    }
    return hit;
}
} }
