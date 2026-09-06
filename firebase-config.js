import { initializeApp } from 'https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js';
import { getAnalytics } from 'https://www.gstatic.com/firebasejs/10.12.2/firebase-analytics.js';
import { getDatabase } from 'https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js';
import { getAuth, signInAnonymously } from 'https://www.gstatic.com/firebasejs/10.12.2/firebase-auth.js';

export const firebaseConfig = {
  apiKey: 'AIzaSyB2cWXPm2ROHxdSdS2bQelDQSq832lj72M',
  authDomain: 'poultry-farm-management-2abc5.firebaseapp.com',
  projectId: 'poultry-farm-management-2abc5',
  databaseURL: 'https://poultry-farm-management-2abc5-default-rtdb.firebaseio.com',
  storageBucket: 'poultry-farm-management-2abc5.firebasestorage.app',
  messagingSenderId: '447832969269',
  appId: '1:447832969269:web:935efd11ca861be1fbe17c',
  measurementId: 'G-CDS7CSNNJD'
};

export const firebaseApp = initializeApp(firebaseConfig);
export const db = getDatabase(firebaseApp);
export const auth = getAuth(firebaseApp);

export const authReady = signInAnonymously(auth).catch((error) => {
  console.error('Firebase anonymous authentication failed.', error);
  throw error;
});

try {
  getAnalytics(firebaseApp);
} catch (error) {
  console.warn('Firebase Analytics is unavailable in this browser.', error);
}
