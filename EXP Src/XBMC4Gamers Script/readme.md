# Script Installation

* 1 Place the service_type_d.py file in Q:\system\scripts on your XBOX

* 2 Edit the following files: Home.xml and Startup.xml in this example they will be located in E:\Dashboard\skins\Profile\xml

* 3 in the onload secion of both files include the following:

	```
		<onload condition="Skin.HasSetting(HomeReloadCustoms)">RunScript(Special://scripts\XBMC4Gamers\Utilities\Parse Programs DB.py,refresh_media)</onload>
		<onload condition="!Window(Home).Property(TypeDService)">RunScript(q:\system\scripts\service_type_d.py)</onload>	
	```
	
* 4 Reboot your XBOX and enjoy
