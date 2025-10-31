const gameArea = document.getElementById('gameArea');
const gameContainer = document.getElementById('gameContainer');
const COLUMNS = 4;
let ROWS = 100;
let currentTileType = 'tap';
let isDragging = false;
let dragStartRow = null;
let dragColumn = null;
let dragPreview = null;
let tilesData = [];
const timePerTile = 200;

function setTileType(type){
  currentTileType = type;
  document.querySelectorAll('.typeToggle').forEach(btn=>btn.classList.remove('active'));
  if(type==='tap') document.getElementById('tapBtn').classList.add('active');
  if(type==='hold') document.getElementById('holdBtn').classList.add('active');
}

function createGrid(){
  gameArea.innerHTML='';
  for(let r=0;r<ROWS;r++){
    for(let c=0;c<COLUMNS;c++){
      const tile=document.createElement('div');
      tile.classList.add('tile');
      tile.dataset.row=r;
      tile.dataset.col=c;
      tile.addEventListener('mousedown',()=>onMouseDown(tile));
      tile.addEventListener('mouseenter',()=>onMouseEnter(tile));
      tile.addEventListener('mouseup',()=>onMouseUp(tile));
      gameArea.appendChild(tile);
    }
  }
  setTimeout(()=>{gameContainer.scrollTop=gameContainer.scrollHeight;},50);
  renderTiles();
}

createGrid();

function rowToPress(row){
  return (ROWS - row - 1) * timePerTile;
}

function clearTileVisualAll(){
  document.querySelectorAll('.tile').forEach(tile=>tile.classList.remove('active','hold'));
}

function renderTiles(){
  clearTileVisualAll();
  tilesData.forEach(t=>{
    if(t.type==='tap'){
      const tile=document.querySelector(`.tile[data-col='${t.col}'][data-row='${t.startRow}']`);
      if(tile) tile.classList.add('active');
    } else if(t.type==='hold'){
      for(let r=t.startRow;r<=t.endRow;r++){
        const tile=document.querySelector(`.tile[data-col='${t.col}'][data-row='${r}']`);
        if(tile) tile.classList.add('active','hold');
      }
    }
  });
  if(dragPreview){
    for(let r=dragPreview.startRow;r<=dragPreview.endRow;r++){
      const tile=document.querySelector(`.tile[data-col='${dragPreview.col}'][data-row='${r}']`);
      if(tile) tile.classList.add('active','hold');
    }
  }
}

function onMouseDown(tile){
  const col = parseInt(tile.dataset.col);
  const row = parseInt(tile.dataset.row);

  if(currentTileType==='tap'){
    const holdIndex = tilesData.findIndex(t => t.type==='hold' && t.col===col && row >= t.startRow && row <= t.endRow);
    if(holdIndex !== -1){
      tilesData.splice(holdIndex, 1);
      renderTiles();
      return;
    }
    const tapIndex = tilesData.findIndex(t => t.type==='tap' && t.col===col && t.startRow===row);
    if(tapIndex !== -1){
      tilesData.splice(tapIndex, 1);
    } else {
      tilesData.push({type:'tap', col, startRow:row, endRow:row, press:rowToPress(row), release:null});
    }
    renderTiles();
  } else if(currentTileType==='hold'){
    isDragging=true;
    dragStartRow=row;
    dragColumn=col;
    dragPreview={startRow: row, endRow: row, col: col};
    renderTiles();
  }
}

function onMouseEnter(tile){
  if(isDragging && currentTileType==='hold'){
    const row=parseInt(tile.dataset.row);
    const col=parseInt(tile.dataset.col);
    if(col===dragColumn){
      dragPreview.startRow = Math.min(dragStartRow,row);
      dragPreview.endRow = Math.max(dragStartRow,row);
      renderTiles();
    }
  }
}

function onMouseUp(tile){
  if(isDragging && currentTileType==='hold'){
    finalizeHold(dragPreview.startRow, dragPreview.endRow, dragPreview.col);
    dragPreview=null;
  }
  isDragging=false;
  dragStartRow=null;
  dragColumn=null;
}

function finalizeHold(startRow,endRow,col){
  if(startRow===endRow) return;

  let newStart=startRow, newEnd=endRow;
  tilesData = tilesData.filter(t=>{
    if(t.type==='hold' && t.col===col && t.endRow>=newStart && t.startRow<=newEnd){
      newStart=Math.min(newStart,t.startRow);
      newEnd=Math.max(newEnd,t.endRow);
      return false;
    }
    return true;
  });

  tilesData = tilesData.filter(t=>!(t.type==='tap' && t.col===col && t.startRow>=newStart && t.startRow<=newEnd));

  tilesData.push({
    type:'hold',
    col,
    startRow:newStart,
    endRow:newEnd,
    press: rowToPress(newEnd),
    release: rowToPress(newStart)
  });

  renderTiles();
}

function resizeGrid(){
  const songLength=parseFloat(document.getElementById('length').value)||10;
  ROWS = Math.ceil((songLength*1000)/timePerTile);
  tilesData=[];
  createGrid();
}

function exportJSON(){
  const songName=document.getElementById('songName').value||'Unknown';
  const artist=document.getElementById('artist').value||'Unknown';
  const length=parseInt(document.getElementById('length').value)||0;

  const sortedTiles = [...tilesData].sort((a,b)=>a.press-b.press);
  const tilesOutput = sortedTiles.map(t=>({type:t.type, press:t.press, slot:t.col+1, release:t.release}));

  document.getElementById('jsonOutput').value = JSON.stringify({songName,artist,length,tiles:tilesOutput},null,2);
}