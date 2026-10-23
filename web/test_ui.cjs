/* DOM-contract smoke check using a minimal stub, not browser rendering.
 * Validates event wiring, real/demo isolation, API parameters, and cancelled reads.
 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function fixture() {
  const html = fs.readFileSync(path.join(__dirname,'plugin.html'),'utf8');
  const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(match=>match[1]);
  assert.equal(new Set(ids).size, ids.length, 'all HTML IDs unique');
  const elements = new Map();
  const classList = () => ({toggle(){}});
  const handlers = () => Object.create(null);
  const context2d = new Proxy({}, {get(target,key) { return target[key] || (()=>{}); }, set(target,key,value) {target[key]=value;return true;}});
  function element(id) {
    return {id,textContent:'',value:'',hidden:false,className:'',classList:classList(),events:handlers(),style:{},
      addEventListener(name,handler){this.events[name]=handler;},
      setAttribute(){},removeAttribute(){},showModal(){this.open=true;},close(){this.open=false;},
      getBoundingClientRect(){return {width:400,height:180};},getContext(){return context2d;},
      appendChild(){},click(){},remove(){}};
  }
  for (const id of ids) elements.set(id,element(id));
  const storage=new Map(),timeouts=new Map(),intervals=new Map();let timerId=0;
  const document={hidden:false,body:element('body'),events:handlers(),getElementById(id){assert(elements.has(id),'JS refers to a real HTML element: '+id);return elements.get(id);},addEventListener(name,handler){this.events[name]=handler;},createElement(){return element('created');}};
  const window={devicePixelRatio:1,events:handlers(),localStorage:{getItem:k=>storage.get(k)||null,setItem:(k,v)=>storage.set(k,v),removeItem:k=>storage.delete(k)},setInterval(fn){const id=++timerId;intervals.set(id,fn);return id;},clearInterval(id){intervals.delete(id);},setTimeout(fn,ms){const id=++timerId;timeouts.set(id,{fn,ms});return id;},clearTimeout(id){timeouts.delete(id);},addEventListener(name,handler){this.events[name]=handler;}};
  const fetches=[];let responder=()=>Promise.resolve({ok:true,json:()=>Promise.resolve({feeds:[]})});
  const ctx={window,document,URL,Blob,AbortController,console,Date,Set,Number,Math,Array,Object,fetch:(url,options)=>{fetches.push({url,options});return responder(url,options);}};
  vm.runInNewContext(fs.readFileSync(path.join(__dirname,'plugin.js'),'utf8'),ctx,{filename:'plugin.js'});
  return {elements,storage,timeouts,intervals,fetches,responder:fn=>{responder=fn;},get:id=>elements.get(id),click:id=>elements.get(id).events.click(),submit:()=>elements.get('config-form').events.submit({preventDefault(){}})};
}
const tick=()=>new Promise(resolve=>setImmediate(resolve));
const feed=(status)=>({entry_id:1,created_at:new Date().toISOString(),field1:'220',field2:'1',field3:'198',field4:'1.25',field5:'60',field6:'0.9',field7:'0.2',field8:'0.5',status:status||'ver=2;state=RUN;e=1;v=1;ea=200;va=100;alarm=0;ref=1;run=1;ot=1;rc=10;ct=0;vx=0.1;vy=0.12;vz=0.14;cf=2.5;fs=500;ns=500;mi=0;rj=0;rp=0;dp=0;vr=1;boot=3;seq=4;vf=1'});

(async()=>{
  const ui=fixture();
  assert.equal(ui.get('power-value').textContent,'—');assert.equal(ui.fetches.length,0);
  ui.click('demo-button');
  assert.equal(ui.get('demo-notice').hidden,false);assert.match(ui.get('mode-pill').textContent,/DEMO/);assert.notEqual(ui.get('power-value').textContent,'—');assert.equal(ui.fetches.length,0);
  ui.click('stop-demo');assert.equal(ui.get('demo-notice').hidden,true);assert.equal(ui.get('power-value').textContent,'—');
  ui.click('configure-button');assert.equal(ui.get('config-dialog').open,true);
  ui.get('channel-input').value='1234567';ui.get('key-input').value='READSECRET1234567';
  ui.responder(()=>Promise.resolve({ok:true,json:()=>Promise.resolve({feeds:[feed()]})}));
  ui.submit();await tick();await tick();
  assert.equal(ui.fetches.length,1);assert.match(ui.fetches[0].url,/results=100/);assert.match(ui.fetches[0].url,/status=true/);assert.match(ui.fetches[0].url,/api_key=READSECRET1234567/);
  assert.equal(ui.fetches[0].options.credentials,'omit');assert.equal(ui.storage.size,1);assert.equal([...ui.storage.values()][0],'1234567');
  assert.notEqual(ui.get('power-value').textContent,'—');assert.equal(ui.get('machine-state').textContent,'OPERANDO');assert.equal(ui.get('demo-notice').hidden,true);
  // An invalid electrical group must not be displayed as zero or reuse old values.
  const timer=[...ui.timeouts.values()].find(item=>item.ms===20000);assert(timer);
  ui.responder(()=>Promise.resolve({ok:true,json:()=>Promise.resolve({feeds:[feed('ver=2;state=WAIT;e=0;v=1;ea=200;va=100;alarm=0;ref=1;run=1;ot=1;rc=10;ct=0;vx=0.1;vy=0.12;vz=0.14;cf=2.5;fs=500;ns=500;mi=0;rj=0;rp=0;dp=na;vr=na;boot=3;seq=5;vf=1')]})}));
  timer.fn();await tick();await tick();
  assert.equal(ui.get('power-value').textContent,'—');assert.notEqual(ui.get('vib-rms-value').textContent,'—');assert.equal(ui.get('machine-state').textContent,'SIN LECTURA');
  // Reset while an old request is pending: its late response cannot restore state.
  let resolveRequest;
  ui.responder(()=>new Promise(resolve=>{resolveRequest=resolve;}));
  [...ui.timeouts.values()].filter(item=>item.ms===20000).at(-1).fn();
  ui.click('reset-button');assert.equal(ui.storage.size,0);assert.equal(ui.get('key-input').value,'');
  resolveRequest({ok:true,json:()=>Promise.resolve({feeds:[feed()]})});await tick();await tick();
  assert.equal(ui.get('power-value').textContent,'—');assert.equal(ui.get('mode-pill').textContent,'SIN CONEXIÓN');
  console.log('E-TWIN UI: IDs, initial empty state, explicit demo, real API options, non-persisted key, invalid sensors, and cancellation checks passed (stub DOM; no browser visual QA).');
})().catch(error=>{console.error(error);process.exitCode=1;});
