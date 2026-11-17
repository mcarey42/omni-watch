// The Omni glance frame (design/shots/omni-dial-glance.png): the omni-dial entry's sleepFrame through the export page.
// Usage: node tools/omni_glance_shot.mjs <unix ms> <out.png>   (the shot is 2026-10-22 14:10:40 UTC = 1792678240000)
// CHROME=/usr/bin/google-chrome by default; run tools/fonts.sh first.
import fs from 'node:fs'; import http from 'node:http'; import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
const root=path.join(path.dirname(fileURLToPath(import.meta.url)),'..');
const { chromium } = createRequire(root+'/tools/')('playwright-core');
const TYPES={'.html':'text/html','.js':'text/javascript','.ttf':'font/ttf','.jpg':'image/jpeg'};
const server=http.createServer((req,res)=>{const p=path.join(root,decodeURIComponent(new URL(req.url,'http://x').pathname));
 if(!fs.existsSync(p)||!fs.statSync(p).isFile()){res.writeHead(404);res.end();return;}
 res.writeHead(200,{'content-type':TYPES[path.extname(p)]||'application/octet-stream'});fs.createReadStream(p).pipe(res);});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const b=await chromium.launch({executablePath:'/usr/bin/google-chrome',headless:true,args:['--disable-gpu','--force-color-profile=srgb']});
const ctx=await b.newContext({timezoneId:'America/New_York'});
await ctx.route(/fonts\.(googleapis|gstatic)/,r=>r.abort());
const page=await ctx.newPage();  page.on('pageerror',e=>console.log('ERR',e.message));
await page.goto(`http://127.0.0.1:${server.address().port}/tools/export/export.html`,{waitUntil:'load'});
const [,,unix,out]=process.argv;
const url=await page.evaluate(async (ms)=>{
 await document.fonts.load('700 13px "Cormorant SC"');
 const W=466,c=document.createElement('canvas');c.width=c.height=W;const g=c.getContext('2d');
 g.fillStyle='#000';g.fillRect(0,0,W,W);
 const f=window.OMNI_FACES.find(x=>x.id==='omni-dial');
 const env={W,cx:233,cy:233,R:233,assets:{},reducedMotion:false,awake:false,now:()=>new Date(ms),location:{lat:39.96,lon:-83},goto(){}};
 g.save();f.sleepFrame(g,0,env);g.restore();
 g.globalCompositeOperation='destination-in';g.beginPath();g.arc(233,233,233,0,Math.PI*2);g.fill();
 return c.toDataURL('image/png');}, +unix);
fs.writeFileSync(out,Buffer.from(url.split(',')[1],'base64'));
await b.close();server.close();
