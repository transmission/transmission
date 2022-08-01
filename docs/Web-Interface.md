A web interface is built into all Transmission flavors, enabling them to be controlled remotely.

## Enabling the web interface ##
### 1. For Windows ###
Open Transmission Qt. Go to Edit menu   

![image](https://user-images.githubusercontent.com/76732207/179838374-f97fe3d5-b66f-4193-868d-9454e46c9eff.png)

Click "Preferences". Then go to tab "Remote"

![image](https://user-images.githubusercontent.com/76732207/179838907-c1e61fb4-d1e3-4743-90d7-b65ce9e26969.png)

Click on "Allow remote access" checkbox. If password protection is required, click on "Use authentication" checkbox, set username and password. If "Only allow these IP adresses" is checked, Transmission will only allow the specified list of addresses to access the web interface.

![image](https://user-images.githubusercontent.com/76732207/179840681-8d9db5b3-4bb3-4884-9686-1794a331165f.png)

Click "Close" button. Done!


### 2. For Linux ###
Open Transmission. Go to Edit menu and click "Preferences".

![Screenshot_2022-07-20_23-47-00](https://user-images.githubusercontent.com/76732207/180082230-47658410-4e3a-456d-bd96-95be5db251b4.png)

Then go to tab "Remote"

![Screenshot_2022-07-20_23-49-18](https://user-images.githubusercontent.com/76732207/180082407-9a37db4a-63bf-42f4-b5e7-264521f791c2.png)

Click on "Allow remote access" checkbox. If password protection is required, click on "Use authentication" checkbox, set username and password. If "Only allow these IP adresses" is checked, Transmission will only allow the specified list of addresses to access the web interface.

![image](https://user-images.githubusercontent.com/76732207/180082691-3caf5ba6-406f-4013-9ca4-1be650f17c94.png)

Click "Close" button. Done!

## Accessing the web interface ##
Once enabled, open a web browser and direct it to http://ip_address_of_machine_running_transmission:9091/
If the web browser and the Transmission daemon are on the machine you can use http://127.0.0.1:9091/
9091 is the default remote control port specified in [Transmission configuration](Editing-Configuration-Files.md) or in preferences of [Windows](https://github.com/transmission/transmission/blob/main/docs/Web-Interface.md#1-for-windows) or [Linux](https://github.com/transmission/transmission/blob/main/docs/Web-Interface.md#2-for-linux) client.

## Web Interface Overview ##
### 1. Main Screen ###
![image](https://user-images.githubusercontent.com/76732207/181889508-c926e97c-f51a-4562-bf8b-75e4be672786.png)

| Number On Picture | Description                 |
| ----------------- | --------------------------- |
| 1                 | Torrent name                |
| 2                 | Torrent management panel    |
| 3                 | Info about selected torrent |
| 4                 | Transmission control panel  |

### 2. Torrent management panel ###
![image](https://user-images.githubusercontent.com/76732207/181903463-f3752531-6a5e-41e8-bea0-fdcd4fabed94.png)

| Number On Picture | Description                       |
| ----------------- | --------------------------------- |
| 1                 | Open Torrent                      |
| 2                 | Remove selected torrents          |
| 3                 | Start selected torrents           |
| 4                 | Pause selected torrents           |
| 5                 | Start all torrents                |
| 6                 | Pause all torrents                |
| 7                 | View info about selected torrents |

#### 2.1 Adding torrent ####
![image](https://user-images.githubusercontent.com/76732207/181906789-b56f8e8c-c362-407b-bbd7-d6df83c54f08.png)

| Number On Picture | Description                                       |
| ----------------- | ------------------------------------------------- |
| 1                 | Button to select a torrent file to upload         |
| 2                 | Or enter an URL to torrent                        |
| 3                 | Enter the path where the file will be downloaded  |
| 4                 | Autostart torrent download after adding           |
| 5                 | Cancel adding                                     |
| 6                 | Add selected torrent to Transmission              |

### 3. Display filters panel ###
![image](https://user-images.githubusercontent.com/76732207/181907925-34f5b8fe-8a6d-48e2-80e3-df25bf36423c.png)

| Number On Picture | Description                                           |
| ----------------- | ----------------------------------------------------- |
| 1                 | Filter torrents by status                             |
| 2                 | Filter torrents by source                             |
| 3                 | Filter torrents by keyword                            |
| 4                 | The number of torrents found according to the filters |
| 5                 | Downloading speed                                     |
| 6                 | Uploading speed                                       |

### 4. List of added torrents ###
![image](https://user-images.githubusercontent.com/76732207/181936421-a6d36ca0-e214-4f37-9368-1404cbeb125c.png)

| Number On Picture | Description                                                                     |
| ----------------- | ------------------------------------------------------------------------------- |
| 1                 | Name of torrent file                                                            | 
| 2                 | In current case: the torrent is downloading from 0 peers of 0 possible          |
| 3                 | Download and upload speeds of current torrent                                   |
| 4                 | In current case: the torrent is downloaded on 0 bytes of 3.65GB. This is 0.00 % |
| 5                 | The remaining time until the torrent is fully downloaded                        |
| 6                 | Progress bar showing the current progress in downloading the torrent            |
| 7                 | Pause selected torrent                                                          |


