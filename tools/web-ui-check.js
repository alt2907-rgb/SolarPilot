// Runs the actual embedded browser script against small DOM/API fixtures.
// These checks cover misleading/stale states and command gating, not CSS.
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync('include/web/AdminPage.h','utf8');
const script=html.split('<script>')[1].split('</script>')[0].replace('initialize();setInterval','setInterval');
const ids=[...html.matchAll(/\bid="([^"]+)"/g)].map(m=>m[1]);
assert.equal(new Set(ids).size,ids.length,'Duplicate element IDs');
function element(dataset={}){return {dataset,hidden:false,disabled:false,value:'',textContent:'',className:'',children:[],setAttribute(){},removeAttribute(){},replaceChildren(...c){this.children=c},appendChild(c){this.children.push(c)},add(c){this.children.push(c)}}}
const nodes=Object.fromEntries(ids.map(id=>[id,element()]));
const panels=['overview','devices','wifi','history','maintenance','logs'].map(panel=>element({panel}));
const commands=['diagnose','stop','restart','cycle','goodwe-loss','wifi-loss','switch-failure'].map(command=>element({command}));
const context={console,URLSearchParams,AbortController,Option:function(text,value){this.text=text;this.value=value},location:{pathname:'/admin',hash:''},window:{addEventListener(){}},document:{hidden:false,getElementById:id=>{assert(nodes[id],id);return nodes[id]},createElement:()=>element(),querySelectorAll:selector=>selector==='[data-panel]'?panels:selector==='[data-command]'?commands:[]},setInterval(){},setTimeout(){return 0},clearTimeout(){},confirm:()=>true,fetch:async()=>({ok:true,json:async()=>({token:'fixture',entries:[],active:false,testing:false,can_save:false}),text:async()=>''})};
vm.createContext(context);vm.runInContext(script,context);
const run=code=>vm.runInContext(code,context);
const status={health:'In Ordnung',power:-420,valid:true,age:1000,on:true,real:true,pending:false,retry:false,rssi:-58,source:'goodwe-et',source_connected:true,mode:'normal',test:false,retries:2,timeouts:1,probes:0,rules:{on_w:50,off_w:20,on_seconds:15,off_seconds:10,safe_seconds:30}};
context.sample=status;run('renderStatus(sample)');assert.equal(nodes.power.textContent,'420 W');assert.equal(nodes.direction.textContent,'Strombezug aus dem Netz');assert.equal(nodes.ruleSafe.textContent,'30 Sekunden');
run('offline()');assert.equal(nodes.output.textContent,'Unbekannt');assert.equal(nodes.power.textContent,'—');
status.valid=false;status.mode='setup';status.on=false;run('renderStatus(sample)');assert.match(nodes.connection.textContent,/Einrichtung/);assert.doesNotMatch(nodes.modeHint.textContent,/Testbetrieb:/);
context.setup={active:true,testing:true,can_save:false,remaining_seconds:300,ap_name:'Fixture',ap_clients:1,ap_enabled:true,ip:'',connected_seconds:5,drops:0};run("token='fixture';renderSetup(setup)");assert(nodes.setupTest.disabled);assert(nodes.setupSave.disabled);assert(commands.every(b=>b.disabled));
context.setup.testing=false;context.setup.tested=true;context.setup.can_save=true;context.setup.connected_seconds=28;run('renderSetup(setup)');assert(!nodes.setupSave.disabled);assert.match(nodes.setupTestResult.textContent,/Bestanden/);
run('candidateChanged();renderSetup(setup)');assert(nodes.setupSave.disabled);assert.match(nodes.setupTestResult.textContent,/Eingabe geändert/);
run('navigate("wifi")');assert(!panels.find(p=>p.dataset.panel==='wifi').hidden);assert(panels.find(p=>p.dataset.panel==='overview').hidden);
run('navigate("unknown")');assert(!panels.find(p=>p.dataset.panel==='overview').hidden);
console.log('OK: Browser script, stale states, mode labels, rules, test gating and navigation.');
