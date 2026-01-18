// 全局状态
const state = {
    logs: [],
    online: [],
    ops: [],
    banned: [],
    requests: [],
    players: [],
    threshold: 5
};

// API 基础 URL
const API_BASE = '/api';

// 初始化
document.addEventListener('DOMContentLoaded', () => {
    console.log('页面加载完成，开始初始化...');
    initEventListeners();
    loadAllData();
    
    // 定时刷新
    setInterval(loadLogs, 2000);
    setInterval(loadOnline, 5000);
    setInterval(loadRequests, 3000);
});

// 事件监听
function initEventListeners() {
    // 表单提交
    const form = document.getElementById('request-form');
    if (form) {
        form.addEventListener('submit', handleSubmitRequest);
        console.log('表单事件监听器已绑定');
    } else {
        console.error('未找到表单元素！');
    }
    
    // 模态框关闭
    const closeBtn = document.querySelector('.close');
    if (closeBtn) {
        closeBtn.addEventListener('click', () => {
            document.getElementById('image-modal').style.display = 'none';
        });
    }
    
    // 点击模态框外部关闭
    window.addEventListener('click', (e) => {
        const modal = document.getElementById('image-modal');
        if (e.target === modal) {
            modal.style.display = 'none';
        }
    });
}

// 加载所有数据
async function loadAllData() {
    await Promise.all([
        loadLogs(),
        loadOnline(),
        loadOps(),
        loadBanned(),
        loadRequests(),
        loadPlayers()
    ]);
}

// 加载日志
async function loadLogs() {
    try {
        const response = await fetch(`${API_BASE}/logs`);
        const data = await response.json();
        state.logs = data.logs || [];
        renderLogs();
    } catch (error) {
        console.error('加载日志失败:', error);
    }
}

// 加载在线玩家
async function loadOnline() {
    try {
        const response = await fetch(`${API_BASE}/online`);
        const data = await response.json();
        state.online = data.players || [];
        renderOnline();
    } catch (error) {
        console.error('加载在线玩家失败:', error);
    }
}

// 加载OP列表
async function loadOps() {
    try {
        const response = await fetch(`${API_BASE}/ops`);
        const data = await response.json();
        state.ops = data.ops || [];
        renderOps();
    } catch (error) {
        console.error('加载OP列表失败:', error);
    }
}

// 加载被ban玩家
async function loadBanned() {
    try {
        const response = await fetch(`${API_BASE}/banned`);
        const data = await response.json();
        state.banned = data.players || [];
        renderBanned();
    } catch (error) {
        console.error('加载被ban列表失败:', error);
    }
}

// 加载申请列表
async function loadRequests() {
    try {
        const response = await fetch(`${API_BASE}/requests`);
        const data = await response.json();
        state.requests = data.requests || [];
        state.threshold = data.threshold || 5;
        renderRequests();
    } catch (error) {
        console.error('加载申请列表失败:', error);
    }
}

// 加载所有玩家
async function loadPlayers() {
    try {
        const response = await fetch(`${API_BASE}/players`);
        const data = await response.json();
        state.players = data.players || [];
        renderPlayersDatalist();
    } catch (error) {
        console.error('加载玩家列表失败:', error);
    }
}

// 渲染日志
function renderLogs() {
    const container = document.getElementById('log-container');
    if (!container) return;
    
    const shouldScroll = container.scrollHeight - container.scrollTop === container.clientHeight;
    
    container.innerHTML = state.logs.slice(-100).map(log => {
        let content = '';
        switch(log.type) {
            case 'join':
                content = `玩家 [${log.player}] 加入了服务器，客户端为 [${log.content}]`;
                break;
            case 'leave':
                content = `玩家 [${log.player}] 退出了服务器`;
                break;
            case 'command':
                content = `玩家 [${log.player}] 执行了操作 [${log.content}]`;
                break;
            case 'chat':
                content = `&lt;${log.player}&gt; ${escapeHtml(log.content)}`;
                break;
            case 'system':
                content = log.content;
                break;
            default:
                content = log.content;
        }
        
        return `<div class="log-entry ${log.type}">
            <span class="log-timestamp">[${log.timestamp}]</span>
            <span class="log-content">${content}</span>
        </div>`;
    }).join('');
    
    if (shouldScroll) {
        container.scrollTop = container.scrollHeight;
    }
}

// 渲染在线玩家
function renderOnline() {
    const list = document.getElementById('online-list');
    if (!list) return;
    
    if (state.online.length === 0) {
        list.innerHTML = '<li style="color: #999;">暂无在线玩家</li>';
    } else {
        list.innerHTML = state.online.map(p => 
            `<li>🟢 ${escapeHtml(p.name)}</li>`
        ).join('');
    }
}

// 渲染OP列表
function renderOps() {
    const list = document.getElementById('ops-list');
    if (!list) return;
    
    if (state.ops.length === 0) {
        list.innerHTML = '<li style="color: #999;">暂无OP</li>';
    } else {
        list.innerHTML = state.ops.map(op => 
            `<li>👑 ${escapeHtml(op)}</li>`
        ).join('');
    }
}

// 渲染被ban列表
function renderBanned() {
    const container = document.getElementById('banned-list');
    if (!container) return;
    
    if (state.banned.length === 0) {
        container.innerHTML = '<div style="color: #999; text-align: center;">暂无被ban玩家</div>';
    } else {
        container.innerHTML = state.banned.map(p => `
            <div class="banned-item">
                <strong>${escapeHtml(p.name)}</strong>
                <small>封禁: ${p.ban_time}</small>
                <small>解封: ${p.permanent ? '永久' : p.unban_time}</small>
            </div>
        `).join('');
    }
}

// 渲染申请列表
function renderRequests() {
    const container = document.getElementById('requests-list');
    if (!container) return;
    
    if (state.requests.length === 0) {
        container.innerHTML = '<div style="color: #999; text-align: center;">暂无申请</div>';
    } else {
        container.innerHTML = state.requests.map(req => {
            const executed = req.executed || req.votes >= state.threshold;
            return `
                <div class="request-item ${executed ? 'executed' : ''}">
                    <div class="request-header">申请人: ${escapeHtml(req.applicant)}</div>
                    <div class="request-command">${escapeHtml(req.command)}</div>
                    <div>原因: ${escapeHtml(req.reason)}</div>
                    ${req.image ? `<div class="request-image" onclick="showImage('${escapeHtml(req.image)}')">📷 查看检讨书</div>` : ''}
                    <div class="request-votes">票数: ${req.votes}/${state.threshold}</div>
                    <button class="btn btn-vote" 
                            onclick="voteRequest('${req.id}')" 
                            ${executed ? 'disabled' : ''}>
                        ${executed ? '✓ 已执行' : '投票支持'}
                    </button>
                </div>
            `;
        }).join('');
    }
}

// 渲染玩家数据列表
function renderPlayersDatalist() {
    const datalist = document.getElementById('players-datalist');
    if (!datalist) return;
    
    datalist.innerHTML = state.players.map(p => 
        `<option value="${escapeHtml(p)}">`
    ).join('');
}

// 提交申请
async function handleSubmitRequest(e) {
    e.preventDefault();
    console.log('表单提交事件触发');
    
    const form = e.target;
    
    // 手动收集表单数据
    const applicant = document.getElementById('applicant').value.trim();
    const command = document.getElementById('command').value.trim();
    const reason = document.getElementById('reason').value.trim();
    const imageFile = document.getElementById('image').files[0];
    
    console.log('表单数据:', { applicant, command, reason, hasImage: !!imageFile });
    
    // 验证
    if (!applicant || !command || !reason) {
        alert('请填写所有必填字段');
        return;
    }
    
    // 创建 FormData
    const formData = new FormData();
    formData.append('applicant', applicant);
    formData.append('command', command);
    formData.append('reason', reason);
    
    if (imageFile) {
        formData.append('image', imageFile);
        console.log('已添加图片文件:', imageFile.name);
    }
    
    // 调试：打印 FormData 内容
    console.log('FormData 内容:');
    for (let pair of formData.entries()) {
        if (pair[1] instanceof File) {
            console.log(pair[0], '=', pair[1].name, '(文件)');
        } else {
            console.log(pair[0], '=', pair[1]);
        }
    }
    
    try {
        console.log('正在发送请求...');
        const response = await fetch(`${API_BASE}/requests`, {
            method: 'POST',
            body: formData
            // 不设置 Content-Type，让浏览器自动设置 multipart/form-data 边界
        });
        
        console.log('响应状态:', response.status);
        const data = await response.json();
        console.log('响应数据:', data);
        
        if (response.ok) {
            alert('申请提交成功！ID: ' + data.id);
            form.reset();
            loadRequests();
        } else {
            alert('提交失败: ' + (data.error || '未知错误'));
        }
    } catch (error) {
        console.error('提交申请失败:', error);
        alert('提交失败，请重试: ' + error.message);
    }
}

// 投票
async function voteRequest(id) {
    console.log('投票:', id);
    
    try {
        const response = await fetch(`${API_BASE}/requests/${id}/vote`, {
            method: 'POST'
        });
        
        console.log('投票响应状态:', response.status);
        const data = await response.json();
        console.log('投票响应数据:', data);
        
        if (response.ok) {
            alert('投票成功！');
            loadRequests();
        } else {
            alert(data.error || '投票失败');
        }
    } catch (error) {
        console.error('投票失败:', error);
        alert('投票失败，请重试: ' + error.message);
    }
}

// 显示图片
function showImage(imagePath) {
    const modal = document.getElementById('image-modal');
    const img = document.getElementById('modal-image');
    img.src = '/uploads/' + imagePath;
    modal.style.display = 'block';
}

// HTML转义
function escapeHtml(text) {
    if (!text) return '';
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}
