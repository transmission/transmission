A web interface is built into all Transmission flavors, enabling them to be controlled remotely.

## Enabling the web interface ##
### 1. For Windows #
Open Transmission Qt. Go to Edit menu   

![image](https://user-images.githubusercontent.com/76732207/179838374-f97fe3d5-b66f-4193-868d-9454e46c9eff.png)

Click "Preferences". Then go to tab "Remote"

![image](https://user-images.githubusercontent.com/76732207/179838907-c1e61fb4-d1e3-4743-90d7-b65ce9e26969.png)

Click on "Allow remote access" checkbox. If password protection is required, click on "Use authentication" checkbox, set username and password. If "Only allow these IP adresses" is checked, Transmission will only allow the specified list of addresses to access the web interface.

![image](https://user-images.githubusercontent.com/76732207/179840681-8d9db5b3-4bb3-4884-9686-1794a331165f.png)

Click "Close" button. Done!


### 2. For Linux #
Open Transmission. Go to Edit menu and click "Preferences".

![Screenshot_2022-07-20_23-47-00](https://user-images.githubusercontent.com/76732207/180082230-47658410-4e3a-456d-bd96-95be5db251b4.png)

Then go to tab "Remote"

![Screenshot_2022-07-20_23-49-18](https://user-images.githubusercontent.com/76732207/180082407-9a37db4a-63bf-42f4-b5e7-264521f791c2.png)

Click on "Allow remote access" checkbox. If password protection is required, click on "Use authentication" checkbox, set username and password. If "Only allow these IP adresses" is checked, Transmission will only allow the specified list of addresses to access the web interface.

![image](https://user-images.githubusercontent.com/76732207/180082691-3caf5ba6-406f-4013-9ca4-1be650f17c94.png)

Click "Close" button. Done!

## Accessing the web interface ##
Once enabled, open a web browser and direct it to http://ip_address_of_machine_running_transmission:9091/
If the web browser and the Transmission daemon are on the machine you can use http\://127.0.0.1:9091/
9091 is the default remote control port specified in [Transmission configuration](Editing-Configuration-Files.md) or in preferences of [Linux](https://github.com/transmission/transmission/edit/main/docs/Web-Interface.md#1-for-windows) or [Windows](https://github.com/transmission/transmission/edit/main/docs/Web-Interface.md#2-for-linux) client.

