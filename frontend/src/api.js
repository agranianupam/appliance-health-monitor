const API_BASE = import.meta.env.VITE_API_BASE || 'http://localhost:5000/api';
async function request(path, options={}) {
  const response = await fetch(`${API_BASE}${path}`, { ...options, headers:{'Content-Type':'application/json', ...(options.headers||{})} });
  const data = await response.json().catch(()=>null);
  if (!response.ok) throw new Error(data?.error || `API request failed (${response.status})`);
  return data;
}
export const getLatest = () => request('/latest');
export const getHistory = () => request('/history?limit=100');
export const getEvents = () => request('/events');
export const getStatus = () => request('/status');
export const getAck = () => request('/ack');
export const sendCommand = (command) => request('/commands', {method:'POST', body:JSON.stringify(command)});
