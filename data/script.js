// Launch Controller JavaScript v3.5.0709

class LaunchController {
    constructor() {
        this.ws = null;
        this.isConnected = false;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 10;
        this.reconnectDelay = 1000;
        this.heartbeatInterval = null;
        this.currentPad = 0;
        this.lastUpdate = 0;
        this.abortTimer = null;
        this.abortTimeRemaining = 60;
        
        this.systemStatus = {
            system_state: 'UNKNOWN',
            group_authorized: false,
            abort_active: false,
            hardware_safe: false,
            can_arm: false,
            can_launch: false,
            pads: [],
            sensors: {},
            ignitors: {}
        };
        
        this.init();
    }
    
    init() {
        this.setupEventListeners();
        this.setupTabs();
        this.connectWebSocket();
        this.loadSettings();
        this.updateDisplay();
        
        // Start periodic updates
        setInterval(() => this.updateDisplay(), 1000);
    }
    
    setupEventListeners() {
        // Emergency controls
        document.getElementById('emergencyAbort').addEventListener('click', () => {
            this.confirmAction('EMERGENCY ABORT', 'Are you sure you want to initiate an emergency abort?', () => {
                this.sendCommand('/api/abort', {});
            });
        });
        
        document.getElementById('emergencyDisarm').addEventListener('click', () => {
            this.confirmAction('DISARM ALL', 'Are you sure you want to disarm all ignitors?', () => {
                this.sendCommand('/api/disarm', {});
            });
        });
        
        // Authorization controls
        document.getElementById('authorizeBtn').addEventListener('click', () => {
            this.authorizePad();
        });
        
        document.getElementById('deauthorizeBtn').addEventListener('click', () => {
            this.deauthorizePad();
        });
        
        // Launch controls
        document.getElementById('armBtn').addEventListener('click', () => {
            this.armIgnitor();
        });
        
        document.getElementById('fireBtn').addEventListener('click', () => {
            this.fireIgnitor();
        });
        
        document.getElementById('disarmBtn').addEventListener('click', () => {
            this.disarmIgnitor();
        });
        
        // Servo controls
        document.getElementById('servoAngle').addEventListener('input', (e) => {
            document.getElementById('servoAngleValue').textContent = e.target.value + '°';
        });
        
        document.getElementById('setServoBtn').addEventListener('click', () => {
            this.setServoPosition();
        });
        
        document.getElementById('centerServoBtn').addEventListener('click', () => {
            document.getElementById('servoAngle').value = 90;
            document.getElementById('servoAngleValue').textContent = '90°';
            this.setServoPosition();
        });
        
        // Preset servo buttons
        document.querySelectorAll('.preset-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const angle = e.target.dataset.angle;
                document.getElementById('servoAngle').value = angle;
                document.getElementById('servoAngleValue').textContent = angle + '°';
                this.setServoPosition();
            });
        });
        
        // Settings and utility controls
        document.getElementById('updateBtn').addEventListener('click', () => {
            this.uploadFirmware();
        });
        
        document.getElementById('refreshLogs').addEventListener('click', () => {
            this.loadLogs();
        });
        
        document.getElementById('downloadLogs').addEventListener('click', () => {
            this.downloadLogs();
        });
        
        document.getElementById('refreshDiag').addEventListener('click', () => {
            this.loadDiagnostics();
        });
        
        document.getElementById('runTest').addEventListener('click', () => {
            this.runHardwareTest();
        });
        
        // Modal controls
        document.getElementById('acknowledgeAbort').addEventListener('click', () => {
            this.acknowledgeAbort();
        });
        
        document.getElementById('closeError').addEventListener('click', () => {
            this.hideModal('errorModal');
        });
        
        // Pad selection changes
        document.getElementById('padSelect').addEventListener('change', (e) => {
            this.currentPad = parseInt(e.target.value);
        });
        
        document.getElementById('launchPadSelect').addEventListener('change', (e) => {
            this.currentPad = parseInt(e.target.value);
            this.updateSafetyChecklist();
        });
        
        document.getElementById('servoPadSelect').addEventListener('change', (e) => {
            this.currentPad = parseInt(e.target.value);
        });
    }
    
    setupTabs() {
        document.querySelectorAll('.tab-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const tabName = e.target.dataset.tab;
                this.switchTab(tabName);
            });
        });
    }
    
    switchTab(tabName) {
        // Update tab buttons
        document.querySelectorAll('.tab-btn').forEach(btn => {
            btn.classList.remove('active');
        });
        document.querySelector(`[data-tab="${tabName}"]`).classList.add('active');
        
        // Update tab content
        document.querySelectorAll('.tab-content').forEach(content => {
            content.classList.remove('active');
        });
        document.getElementById(`${tabName}-tab`).classList.add('active');
        
        // Load tab-specific data
        switch (tabName) {
            case 'logs':
                this.loadLogs();
                break;
            case 'diagnostics':
                this.loadDiagnostics();
                break;
            case 'settings':
                this.loadSettings();
                break;
        }
    }
    
    connectWebSocket() {
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const wsUrl = `${protocol}//${window.location.host}/ws`;
        
        this.ws = new WebSocket(wsUrl);
        
        this.ws.onopen = () => {
            console.log('WebSocket connected');
            this.isConnected = true;
            this.reconnectAttempts = 0;
            this.updateConnectionStatus('Connected', 'connected');
            
            // Start heartbeat
            this.heartbeatInterval = setInterval(() => {
                this.sendWebSocketMessage({
                    command: 'heartbeat',
                    timestamp: Date.now()
                });
            }, 30000);
        };
        
        this.ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                this.handleServerMessage(data);
            } catch (error) {
                console.error('Error parsing WebSocket message:', error);
            }
        };
        
        this.ws.onclose = () => {
            console.log('WebSocket disconnected');
            this.isConnected = false;
            this.updateConnectionStatus('Disconnected', 'disconnected');
            
            if (this.heartbeatInterval) {
                clearInterval(this.heartbeatInterval);
                this.heartbeatInterval = null;
            }
            
            // Attempt reconnection
            if (this.reconnectAttempts < this.maxReconnectAttempts) {
                this.reconnectAttempts++;
                this.updateConnectionStatus(`Reconnecting... (${this.reconnectAttempts}/${this.maxReconnectAttempts})`, 'connecting');
                setTimeout(() => this.connectWebSocket(), this.reconnectDelay);
                this.reconnectDelay = Math.min(this.reconnectDelay * 1.5, 30000);
            } else {
                this.updateConnectionStatus('Connection failed', 'disconnected');
            }
        };
        
        this.ws.onerror = (error) => {
            console.error('WebSocket error:', error);
        };
    }
    
    sendWebSocketMessage(message) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(message));
        }
    }
    
    handleServerMessage(data) {
        if (data.type === 'heartbeat') {
            // Heartbeat response
            return;
        }
        
        // Update system status
        this.systemStatus = { ...this.systemStatus, ...data };
        this.lastUpdate = Date.now();
        this.updateDisplay();
        
        // Handle special events
        if (data.abort_active && !document.getElementById('abortModal').style.display) {
            this.showAbortModal();
        }
        
        if (data.last_error && data.last_error !== '') {
            this.showError(data.last_error);
        }
    }
    
    updateConnectionStatus(text, status) {
        document.getElementById('statusText').textContent = text;
        const indicator = document.getElementById('statusIndicator');
        indicator.className = `status-indicator ${status}`;
    }
    
    updateDisplay() {
        this.updateSystemStatus();
        this.updatePadGrid();
        this.updateSensorReadings();
        this.updateSafetyChecklist();
        this.updateFooter();
    }
    
    updateSystemStatus() {
        document.getElementById('systemState').textContent = this.systemStatus.system_state || '-';
        document.getElementById('systemState').className = `status-value ${this.getStateClass(this.systemStatus.system_state)}`;
        
        this.updateStatusValue('groupAuth', this.systemStatus.group_authorized);
        this.updateStatusValue('abortActive', this.systemStatus.abort_active);
        this.updateStatusValue('hardwareSafe', this.systemStatus.hardware_safe);
        this.updateStatusValue('canArm', this.systemStatus.can_arm);
        this.updateStatusValue('canLaunch', this.systemStatus.can_launch);
    }
    
    updateStatusValue(elementId, value) {
        const element = document.getElementById(elementId);
        if (typeof value === 'boolean') {
            element.textContent = value ? 'Yes' : 'No';
            element.className = `status-value ${value}`;
        } else {
            element.textContent = value || '-';
            element.className = 'status-value';
        }
    }
    
    getStateClass(state) {
        switch (state) {
            case 'IDLE': return 'true';
            case 'ARMED': return 'warning';
            case 'LAUNCHING': return 'warning';
            case 'ABORT': return 'false';
            case 'ERROR': return 'false';
            default: return '';
        }
    }
    
    updatePadGrid() {
        const grid = document.getElementById('padsGrid');
        const pads = this.systemStatus.pads || [];
        
        // Clear existing cards
        grid.innerHTML = '';
        
        // Create cards for active pads
        for (let i = 0; i < 4; i++) {
            const pad = pads.find(p => p.pad_id === i);
            const card = this.createPadCard(i, pad);
            grid.appendChild(card);
        }
    }
    
    createPadCard(padId, pad) {
        const card = document.createElement('div');
        card.className = 'pad-card';
        
        if (pad) {
            // Add state-based classes
            switch (pad.state) {
                case 2: card.classList.add('authorized'); break; // PAD_AUTHORIZED
                case 3: card.classList.add('armed'); break;      // PAD_ARMED
                case 4: card.classList.add('firing'); break;    // PAD_FIRED
                case 5: card.classList.add('error'); break;     // PAD_ERROR
            }
        }
        
        const playerName = pad ? pad.player_name : 'Not assigned';
        const state = pad ? this.getPadStateText(pad.state) : 'Offline';
        const authState = pad ? this.getAuthStateText(pad.auth_state) : 'None';
        const continuity = pad ? (pad.ignitor_continuity ? 'Yes' : 'No') : '-';
        const current = pad ? `${pad.current.toFixed(1)} mA` : '-';
        const hardwareSafe = pad ? (pad.hardware_safe ? 'Yes' : 'No') : '-';
        
        card.innerHTML = `
            <div class="pad-header">
                <h3 class="pad-title">Pad ${padId + 1}</h3>
                <span class="pad-state ${this.getPadStateClass(pad?.state)}">${state}</span>
            </div>
            <div class="pad-info">
                <div class="pad-info-item">
                    <label>Player:</label>
                    <span>${playerName}</span>
                </div>
                <div class="pad-info-item">
                    <label>Authorization:</label>
                    <span>${authState}</span>
                </div>
                <div class="pad-info-item">
                    <label>Continuity:</label>
                    <span class="${continuity === 'Yes' ? 'text-success' : 'text-danger'}">${continuity}</span>
                </div>
                <div class="pad-info-item">
                    <label>Current:</label>
                    <span>${current}</span>
                </div>
                <div class="pad-info-item">
                    <label>Hardware Safe:</label>
                    <span class="${hardwareSafe === 'Yes' ? 'text-success' : 'text-danger'}">${hardwareSafe}</span>
                </div>
            </div>
        `;
        
        return card;
    }
    
    getPadStateText(state) {
        switch (state) {
            case 0: return 'Offline';
            case 1: return 'Idle';
            case 2: return 'Authorized';
            case 3: return 'Armed';
            case 4: return 'Fired';
            case 5: return 'Error';
            default: return 'Unknown';
        }
    }
    
    getPadStateClass(state) {
        switch (state) {
            case 2: return 'success';     // Authorized
            case 3: return 'warning';     // Armed
            case 4: return 'danger';      // Fired
            case 5: return 'error';       // Error
            default: return 'secondary';
        }
    }
    
    getAuthStateText(authState) {
        switch (authState) {
            case 0: return 'None';
            case 1: return 'Pending';
            case 2: return 'Authorized';
            case 3: return 'Expired';
            default: return 'Unknown';
        }
    }
    
    updateSensorReadings() {
        const sensors = this.systemStatus.sensors || {};
        const readings = this.systemStatus.readings || {};
        
        this.updateSensorValue('temperature', readings.temperature, '°C');
        this.updateSensorValue('humidity', readings.humidity, '%');
        this.updateSensorValue('windSpeed', readings.wind_speed, ' m/s');
        this.updateSensorValue('voltage', readings.voltage, 'V');
        this.updateSensorValue('freeHeap', this.systemStatus.free_heap, ' bytes');
        this.updateSensorValue('uptime', this.formatUptime(this.systemStatus.uptime || 0), '');
    }
    
    updateSensorValue(elementId, value, unit) {
        const element = document.getElementById(elementId);
        if (value !== undefined && value !== null) {
            if (typeof value === 'number') {
                element.textContent = value.toFixed(1) + unit;
            } else {
                element.textContent = value + unit;
            }
        } else {
            element.textContent = '-';
        }
    }
    
    formatUptime(milliseconds) {
        const seconds = Math.floor(milliseconds / 1000);
        const hours = Math.floor(seconds / 3600);
        const minutes = Math.floor((seconds % 3600) / 60);
        const secs = seconds % 60;
        return `${hours}:${minutes.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
    }
    
    updateSafetyChecklist() {
        const currentPadData = this.systemStatus.pads?.find(p => p.pad_id === this.currentPad);
        
        document.getElementById('checkAuth').textContent = this.systemStatus.group_authorized ? '✅' : '❌';
        document.getElementById('checkAbort').textContent = !this.systemStatus.abort_active ? '✅' : '❌';
        document.getElementById('checkHardware').textContent = this.systemStatus.hardware_safe ? '✅' : '❌';
        document.getElementById('checkContinuity').textContent = 
            (currentPadData && currentPadData.ignitor_continuity) ? '✅' : '❌';
    }
    
    updateFooter() {
        document.getElementById('connectedClients').textContent = this.systemStatus.connected_clients || 0;
        
        if (this.lastUpdate > 0) {
            const lastUpdateText = new Date(this.lastUpdate).toLocaleTimeString();
            document.getElementById('lastUpdate').textContent = lastUpdateText;
        }
    }
    
    // API Methods
    async sendCommand(endpoint, data) {
        try {
            const response = await fetch(endpoint, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: new URLSearchParams(data)
            });
            
            const result = await response.json();
            
            if (!result.success) {
                this.showError(result.error || 'Command failed');
            }
            
            return result;
        } catch (error) {
            console.error('Command error:', error);
            this.showError('Network error: ' + error.message);
            return { success: false };
        }
    }
    
    async authorizePad() {
        const padId = document.getElementById('padSelect').value;
        const playerName = document.getElementById('playerName').value.trim();
        
        if (!playerName) {
            this.showError('Please enter a player name');
            return;
        }
        
        const result = await this.sendCommand('/api/authorize', {
            pad_id: padId,
            player_name: playerName
        });
        
        if (result.success) {
            document.getElementById('playerName').value = '';
        }
    }
    
    async deauthorizePad() {
        const padId = document.getElementById('padSelect').value;
        
        await this.sendCommand('/api/deauthorize', {
            pad_id: padId
        });
    }
    
    async armIgnitor() {
        const padId = document.getElementById('launchPadSelect').value;
        
        if (!this.systemStatus.can_arm) {
            this.showError('System cannot arm - check safety conditions');
            return;
        }
        
        this.confirmAction('ARM IGNITOR', `Are you sure you want to arm ignitor for Pad ${parseInt(padId) + 1}?`, async () => {
            await this.sendCommand('/api/arm', { pad_id: padId });
        });
    }
    
    async fireIgnitor() {
        const padId = document.getElementById('launchPadSelect').value;
        
        if (!this.systemStatus.can_launch) {
            this.showError('System cannot launch - check safety conditions');
            return;
        }
        
        this.confirmAction('FIRE IGNITOR', `Are you sure you want to FIRE ignitor for Pad ${parseInt(padId) + 1}? This action cannot be undone!`, async () => {
            await this.sendCommand('/api/fire', { pad_id: padId });
        });
    }
    
    async disarmIgnitor() {
        const padId = document.getElementById('launchPadSelect').value;
        
        await this.sendCommand('/api/disarm', { pad_id: padId });
    }
    
    async setServoPosition() {
        const padId = document.getElementById('servoPadSelect').value;
        const angle = document.getElementById('servoAngle').value;
        
        await this.sendCommand('/api/servo', {
            pad_id: padId,
            angle: angle
        });
    }
    
    async acknowledgeAbort() {
        const padId = document.getElementById('padSelect').value;
        
        this.sendWebSocketMessage({
            command: 'acknowledge_abort',
            pad_id: parseInt(padId)
        });
        
        this.hideModal('abortModal');
    }
    
    async loadLogs() {
        const lines = document.getElementById('logLines').value;
        
        try {
            const response = await fetch(`/api/logs?lines=${lines}`);
            const data = await response.json();
            
            document.getElementById('logsContent').textContent = data.logs || 'No logs available';
        } catch (error) {
            document.getElementById('logsContent').textContent = 'Error loading logs: ' + error.message;
        }
    }
    
    async downloadLogs() {
        window.open('/api/logs?download=1', '_blank');
    }
    
    async loadDiagnostics() {
        try {
            const response = await fetch('/api/diagnostics');
            const data = await response.json();
            
            document.getElementById('diagnosticsContent').textContent = JSON.stringify(data, null, 2);
        } catch (error) {
            document.getElementById('diagnosticsContent').textContent = 'Error loading diagnostics: ' + error.message;
        }
    }
    
    async loadSettings() {
        try {
            const response = await fetch('/api/settings');
            const data = await response.json();
            
            document.getElementById('wifiSSID').textContent = data.wifi_ssid || '-';
            document.getElementById('ipAddress').textContent = this.systemStatus.ip_address || '-';
            document.getElementById('apMode').textContent = data.ap_mode ? 'Yes' : 'No';
            document.getElementById('maxPads').textContent = data.max_pads || '-';
            document.getElementById('authTimeout').textContent = (data.auth_timeout / 1000) + 's' || '-';
            document.getElementById('abortTimeout').textContent = (data.abort_timeout / 1000) + 's' || '-';
        } catch (error) {
            console.error('Error loading settings:', error);
        }
    }
    
    async runHardwareTest() {
        const result = await this.sendCommand('/api/test', {});
        if (result.success) {
            setTimeout(() => this.loadDiagnostics(), 2000);
        }
    }
    
    async uploadFirmware() {
        const fileInput = document.getElementById('firmwareFile');
        const file = fileInput.files[0];
        
        if (!file) {
            this.showError('Please select a firmware file');
            return;
        }
        
        if (!file.name.endsWith('.bin')) {
            this.showError('Please select a .bin firmware file');
            return;
        }
        
        this.confirmAction('FIRMWARE UPDATE', 'Are you sure you want to update the firmware? The system will restart after the update.', async () => {
            const formData = new FormData();
            formData.append('firmware', file);
            
            try {
                const response = await fetch('/api/update', {
                    method: 'POST',
                    body: formData
                });
                
                const result = await response.json();
                
                if (result.success) {
                    this.showError('Firmware update successful. System will restart in 5 seconds.');
                    setTimeout(() => {
                        window.location.reload();
                    }, 5000);
                } else {
                    this.showError('Firmware update failed: ' + (result.error || 'Unknown error'));
                }
            } catch (error) {
                this.showError('Upload error: ' + error.message);
            }
        });
    }
    
    // UI Helper Methods
    confirmAction(title, message, callback) {
        if (confirm(`${title}\n\n${message}`)) {
            callback();
        }
    }
    
    showError(message) {
        document.getElementById('errorMessage').textContent = message;
        this.showModal('errorModal');
    }
    
    showModal(modalId) {
        document.getElementById(modalId).style.display = 'block';
    }
    
    hideModal(modalId) {
        document.getElementById(modalId).style.display = 'none';
    }
    
    showAbortModal() {
        this.abortTimeRemaining = 60;
        this.showModal('abortModal');
        
        this.abortTimer = setInterval(() => {
            this.abortTimeRemaining--;
            document.getElementById('abortTimer').textContent = this.abortTimeRemaining;
            
            if (this.abortTimeRemaining <= 0) {
                clearInterval(this.abortTimer);
                this.hideModal('abortModal');
            }
        }, 1000);
    }
}

// Initialize the application when the page loads
document.addEventListener('DOMContentLoaded', () => {
    window.launchController = new LaunchController();
});

// Handle page visibility for connection management
document.addEventListener('visibilitychange', () => {
    if (document.hidden) {
        // Page is hidden, pause some operations
    } else {
        // Page is visible, resume operations
        if (window.launchController && !window.launchController.isConnected) {
            window.launchController.connectWebSocket();
        }
    }
});