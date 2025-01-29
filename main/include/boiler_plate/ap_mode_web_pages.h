/*
 * Copyright 2025 Johan van Zyl
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FAIL_MODE_WEB_PAGE_H
#define FAIL_MODE_WEB_PAGE_H

/**
 * If critical* failures occur, the ESP will Access Point mode (see wifi_connection)
 * The Wi-Fi SSID and password is set with AP_SSID & AP_PASSWORD
 *
 * From here, a Web-Page will be hosted where the user can define valid Wi-Fi connection as well as
 * URL's for version checking and a URL for OTA firmware update.
 *
 * The config will be stored in NVM and the device rebooted
 */
void init_ap_web_pages();

#endif // FAIL_MODE_WEB_PAGE_H
