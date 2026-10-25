// For a different computer/phone, replace localhost with the backend laptop's LAN IP.
const API_BASE = import.meta.env.VITE_API_BASE || 'http://localhost:5000/api';

async function request(path) {
  const response = await fetch(`${API_BASE}${path}`);
  if (!response.ok) throw new Error(`API request failed (${response.status})`);
  return response.json();
}

export const getLatest = () => request('/latest');
export const getHistory = () => request('/history?limit=100');
export const getEvents = () => request('/events');
