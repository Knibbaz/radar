#pragma once
// Dashboard HTML page — shared between device firmware and native browser preview.
// The device injects live data via /api/stats JSON endpoint.
//
// Usage:
//   char buf[8192];
//   web_pages::render_stats_html(buf, sizeof(buf));

#include <stdio.h>
#include <string.h>

namespace web_pages {

// Dashboard HTML page with embedded CSS + JS. The JS fetches /api/stats and
// renders the dashboard client-side. No external assets, no template placeholders.
// The page is split into parts to avoid C string issues with CSS hyphens and JS.
static const char STATS_HTML_HEAD[] =
    "<!DOCTYPE html>"
    "<html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Feedback Kiosk -- Dashboard</title>"
    "<style>"
    "*{box-sizing:border-box}"
    "body{background:radial-gradient(circle at 50% -10%,#0a1f15,#04100a 70%);color:#cdd6d1;"
    "font-family:system-ui,-apple-system,sans-serif;margin:0 auto;padding:20px;max-width:600px;min-height:100vh}"
    "h1{color:#1dff86;font-size:20px;margin:0}.sub{color:#6f8c7d;font-size:12px;margin:2px 0 0}"
    ".t{color:#1dff86;font-size:11px;letter-spacing:1.5px;text-transform:uppercase;margin-bottom:10px;opacity:.85}"
    ".row{display:flex;gap:10px;margin-bottom:14px}"
    ".card{flex:1;background:rgba(10,20,14,.85);border:1px solid #1f3a2b;border-radius:14px;padding:16px;text-align:center}"
    ".num{font-size:36px;font-weight:700;color:#1dff86}"
    ".numb{font-size:36px;font-weight:700;color:#ff5a3c}"
    ".lbl{font-size:12px;color:#9affc8;margin-top:4px}"
    ".pct{font-size:28px;font-weight:700;color:#1dff86}"
    "table{width:100%;border-collapse:collapse;font-size:12px;margin-top:8px}"
    "th{color:#5f7a6c;text-align:left;padding:4px 6px;font-weight:400}"
    "td{padding:4px 6px;border-top:1px solid #1f3a2b}"
    ".bar{height:14px;border-radius:3px;min-width:2px}"
    ".bg{background:#1dff86}.bb{background:#ff5a3c}"
    ".heat{display:grid;grid-template-columns:auto repeat(3,1fr);gap:4px;font-size:11px;margin-top:8px}"
    ".hh{color:#5f7a6c;padding:4px}.hc{padding:6px;border-radius:4px;text-align:center;color:#04100a;font-weight:700}"
    "a{color:#1dff86}.ft{color:#5f7a6c;font-size:12px;text-align:center;margin-top:20px}"
    "</style></head><body>"
    "<h1>Feedback Kiosk</h1><p class=sub>Operator dashboard -- auto-refresh 60s</p>"
    "<div class=row id=today></div>"
    "<div class=t>Last 31 days</div><div id=chart></div>"
    "<div class=t style=margin-top:16px>Weekday x time of day</div><div id=heat></div>"
    "<p class=ft><a href=/>Settings</a> | <a href=/api/stats>JSON</a></p>"
    "<script>"
    "setInterval(function(){fetch('/api/stats').then(function(r){return r.json()}).then(render).catch(function(){})},60000);"
    "function render(d){"
    "var t=d.today||{};"
    "document.getElementById('today').innerHTML="
    "'<div class=card><div class=num>'+(t.good||0)+'</div><div class=lbl>Goed</div></div>'"
    "+'<div class=card><div class=numb>'+(t.bad||0)+'</div><div class=lbl>Ontevreden</div></div>'"
    "+'<div class=card><div class=pct>'+(t.happy_pct||0)+'%</div><div class=lbl>Tevreden</div></div>';"
    "var c='',rows=d.days||[];"
    "for(var i=0;i<rows.length;i++){"
    "var r=rows[i],tot=r.good+r.bad||1;"
    "var ml=['','jan','feb','mrt','apr','mei','jun','jul','aug','sep','okt','nov','dec'];"
    "c+='<tr><td>'+r.d+' '+ml[r.m]+'</td>'"
    "+'<td><div class=bar style=width:'+Math.round(r.good/tot*100)+'% class=bg></div></td>'"
    "+'<td>'+r.good+'</td>'"
    "+'<td><div class=bar style=width:'+Math.round(r.bad/tot*100)+'% class=bb></div></td>'"
    "+'<td>'+r.bad+'</td></tr>';}"
    "document.getElementById('chart').innerHTML="
    "'<table><tr><th>Dag</th><th>Goed</th><th></th><th>Ontevreden</th><th></th></tr>'+c+'</table>';"
    "var h=d.heat||[],hhtml='',wd=['Zo','Ma','Di','Wo','Do','Vr','Za'],dp=['Ochtend','Middag','Avond'];"
    "hhtml+='<div class=heat><div></div>';"
    "for(var p=0;p<3;p++)hhtml+='<div class=hh>'+dp[p]+'</div>';"
    "for(var w=0;w<7;w++){hhtml+='<div class=hh>'+wd[w]+'</div>';"
    "for(var p=0;p<3;p++){"
    "var cell=(h[w]||[])[p]||{good:0,total:0},pct=cell.total>0?Math.round(cell.good/cell.total*100):0;"
    "var clr=(pct<20)?'#1f3a2b':(pct<40)?'#143a2a':(pct<60)?'#1d5a3c':(pct<80)?'#1d7a4e':'#1dff86';"
    "hhtml+='<div class=hc style=background:'+clr+'>'+cell.total+'</div>';}"
    "}"
    "document.getElementById('heat').innerHTML=hhtml;"
    "}"
    "fetch('/api/stats').then(function(r){return r.json()}).then(render).catch(function(){})"
    "</script></body></html>";

static int render_stats_html(char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    const size_t len = sizeof(STATS_HTML_HEAD) - 1;
    if (len >= cap) return -1;
    memcpy(out, STATS_HTML_HEAD, len);
    out[len] = '\0';
    return (int)len;
}

} // namespace web_pages