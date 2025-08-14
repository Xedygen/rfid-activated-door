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

/**
 * Handles HTTP POST requests sent from the ESP32.
 * This is the main entry point for the script.
 * @param {Object} e - The event parameter containing the POST data.
 * @returns {ContentService.TextOutput} A JSON response indicating success or failure.
 */
function doPost(e) {
  let response;
  try {
    // Parse the JSON payload from the ESP32's request.
    const data = JSON.parse(e.postData.contents);

    // Check for the 'operation' field to determine the required action.
    if (data.operation === "LEARN") {
      response = handleLearnOperation(data);
    } else if (data.operation === "LOG") {
      response = handleLogOperation(data);
    } else {
      // If the operation is unknown, return an error.
      throw new Error("Invalid operation specified.");
    }
  } catch (error) {
    // Catch any errors (e.g., JSON parsing, invalid operation) and create an error response.
    Logger.log(`Error in doPost: ${error.toString()}`);
    response = {
      status: "error",
      message: error.message
    };
  }

  // Return the response as a JSON string.
  return ContentService.createTextOutput(JSON.stringify(response))
    .setMimeType(ContentService.MimeType.JSON);
}

/**
 * Handles the 'LEARN' operation. Adds a new UID to the UIDs sheet.
 * This function is now more robust with LockService and better error checking.
 * @param {Object} data - The parsed JSON data from the request.
 * @returns {Object} A response object.
 */
function handleLearnOperation(data) {
  const lock = LockService.getScriptLock();
  // Wait up to 20 seconds for other processes to finish.
  lock.waitLock(20000); 
  
  try {
    if (!data.uid) {
      throw new Error("LEARN operation requires a 'uid' field.");
    }

    const uidSheet = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(UID_SHEET_NAME);
    if (!uidSheet) {
      throw new Error(`Sheet with name "${UID_SHEET_NAME}" not found.`);
    }

    // Check if the UID already exists to avoid duplicates.
    // Get the last row, or 1 if the sheet is empty.
    const lastRow = uidSheet.getLastRow();
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

    // If not a duplicate, append the new UID to the sheet.
    uidSheet.appendRow([data.uid]);
    Logger.log(`Successfully appended new UID: ${data.uid}`);
    
    return {
      status: "success",
      message: `UID ${data.uid} learned successfully.`
    };

  } catch (error) {
    Logger.log(`Error in handleLearnOperation: ${error.toString()}`);
    // Re-throw the error to be caught by the main doPost catch block
    throw error;
  } finally {
    // ALWAYS release the lock, even if there was an error.
    lock.releaseLock();
  }
}


/**
 * Handles the 'LOG' operation. Appends an access log entry to the Logs sheet.
 * @param {Object} data - The parsed JSON data from the request.
 * @returns {Object} A response object.
 */
function handleLogOperation(data) {
  // Validate that all required fields are present.
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

  return {
    status: "success",
    message: "Log entry recorded."
  };
}
