/**
 * @license
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// --- CONFIGURATION ---
const UID_SHEET_NAME = "UIDs"; // The name of the sheet where authorized UIDs are stored.
const LOG_SHEET_NAME = "Logs"; // The name of the sheet where access logs will be written.

// Map specific ADMIN UIDs to background colors for the log.
// Use standard hex color codes.
const ADMIN_LOG_COLORS = {
  "7B:69:F8:11": "#d9ead3", // Light Green
  "7B:16:01:11": "#cfe2f3"  // Light Blue
  // Add more ADMIN UID -> color mappings here
};


/**
 * Handles HTTP POST requests sent from the ESP32.
 * This is the main entry point for the script.
 * @param {Object} e - The event parameter containing the POST data.
 * @returns {ContentService.TextOutput} A JSON response indicating success or failure.
 */
function doPost(e) {
  let response;
  try {
    const data = JSON.parse(e.postData.contents);

    if (data.operation === "LEARN") {
      response = handleLearnOperation(data);
    } else if (data.operation === "LOG") {
      response = handleLogOperation(data);
    } else {
      throw new Error("Invalid operation specified.");
    }
  } catch (error) {
    Logger.log(`Error in doPost: ${error.toString()}`);
    response = {
      status: "error",
      message: error.message
    };
  }

  return ContentService.createTextOutput(JSON.stringify(response))
    .setMimeType(ContentService.MimeType.JSON);
}

/**
 * Handles the 'LEARN' operation. Adds a new UID and a label to the UIDs sheet.
 * @param {Object} data - The parsed JSON data from the request.
 * @returns {Object} A response object.
 */
function handleLearnOperation(data) {
  const lock = LockService.getScriptLock();
  lock.waitLock(20000); 
  
  try {
    if (!data.uid) {
      throw new Error("LEARN operation requires a 'uid' field.");
    }

    const uidSheet = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(UID_SHEET_NAME);
    if (!uidSheet) {
      throw new Error(`Sheet with name "${UID_SHEET_NAME}" not found.`);
    }

    const lastRow = uidSheet.getLastRow();

    // Add headers if the sheet is empty
    if (lastRow === 0) {
      uidSheet.appendRow(["Card UID", "Card Label"]);
    }
    
    // Check if the UID already exists to avoid duplicates.
    const checkRange = lastRow > 0 ? uidSheet.getRange(1, 1, lastRow, 1) : null;
    let isDuplicate = false;

    if (checkRange) {
        const existingUIDs = checkRange.getValues();
        isDuplicate = existingUIDs.some(row => row[0] == data.uid);
    }

    if (isDuplicate) {
      Logger.log(`UID ${data.uid} already exists. No action taken.`);
      return {
        status: "success",
        message: `UID ${data.uid} already exists.`
      };
    }

    // Create a label for the new guest card
    const guestNumber = lastRow === 0 ? 1 : lastRow; // If headers were just added, this is guest 1
    const cardLabel = `Guest Card ${guestNumber}`;

    // Append the new UID and its label
    uidSheet.appendRow([data.uid, cardLabel]);
    Logger.log(`Successfully appended new UID: ${data.uid} with label: ${cardLabel}`);
    
    return {
      status: "success",
      message: `UID ${data.uid} learned successfully.`
    };

  } catch (error) {
    Logger.log(`Error in handleLearnOperation: ${error.toString()}`);
    throw error;
  } finally {
    lock.releaseLock();
  }
}


/**
 * Handles the 'LOG' operation. Appends an access log entry and color-codes admin entries.
 * @param {Object} data - The parsed JSON data from the request.
 * @returns {Object} A response object.
 */
function handleLogOperation(data) {
  if (!data.timestamp || !data.uid || !data.location || !data.role) {
    throw new Error("LOG operation requires 'timestamp', 'uid', 'location', and 'role' fields.");
  }

  const logSheet = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(LOG_SHEET_NAME);
  if (!logSheet) {
    throw new Error(`Sheet with name "${LOG_SHEET_NAME}" not found.`);
  }

  // If the log sheet is empty, add headers first.
  if (logSheet.getLastRow() === 0) {
    logSheet.appendRow(["Timestamp", "UID", "Location", "Role", "Access Status"]);
  }
  
  const accessStatus = (data.role === 'GUEST' || data.role === 'ADMIN') ? 'Granted' : 'Denied';

  // Append the new log entry as a row.
  logSheet.appendRow([data.timestamp, data.uid, data.location, data.role, accessStatus]);

  // If the role was ADMIN, color the row based on the UID
  if (data.role === "ADMIN" && ADMIN_LOG_COLORS[data.uid]) {
    const newRow = logSheet.getLastRow();
    const color = ADMIN_LOG_COLORS[data.uid];
    logSheet.getRange(newRow, 1, 1, logSheet.getLastColumn()).setBackground(color);
  }

  return {
    status: "success",
    message: "Log entry recorded."
  };
}
