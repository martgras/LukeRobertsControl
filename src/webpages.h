const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>

<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Luke Roberts Control</title>
    <style>
        html {
            font-family: Arial;
            display: inline-block;
            text-align: center;
        }

        h2 {
            font-size: 2.3rem;
        }

        p {
            font-size: 1.9rem;
        }

        body {
            max-width: 400px;
            margin: 0px auto;
            padding-bottom: 25px;
        }


        .slider {
            -webkit-appearance: none;
            width: 100%%;
            height: 15px;
            border-radius: 5px;
            background: #d3d3d3;
            outline: none;
            opacity: 0.7;
            -webkit-transition: .2s;
            transition: opacity .2s;
        }

        .slider::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 25px;
            height: 25px;
            border-radius: 50%%;
            background: #495ab1;
            cursor: pointer;
        }

        .slider::-moz-range-thumb {
            width: 25px;
            height: 25px;
            border-radius: 50%%;
            background: #4CAF50;
            cursor: pointer;
        }

        .switch {
            position: relative;
            display: inline-block;
            width: 60px;
            height: 34px;
        }

        .switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }

        .btnslider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: #ccc;
            -webkit-transition: .4s;
            transition: .4s;
        }

        .btnslider:before {
            position: absolute;
            content: "";
            height: 26px;
            width: 26px;
            left: 4px;
            bottom: 4px;
            background-color: white;
            -webkit-transition: .4s;
            transition: .4s;
        }

        input:checked+.btnslider {
            background-color: #4CAF50;
        }

        input:focus+.btnslider {
            box-shadow: 0 0 1px #2196F3;
        }

        input:checked+.btnslider:before {
            -webkit-transform: translateX(26px);
            -ms-transform: translateX(26px);
            transform: translateX(26px);
        }

        .btnslider.round {
            border-radius: 34px;
        }

        .btnslider.round:before {
            border-radius: 50%%;
        }

        table {
            width: 90;
            vertical-align: middle
        }

        td {
            vertical-align: bottom;
        }

select {
    display: block;
    font-size: 1.5em;
    padding: .6em 1.4em .5em .8em;
    width: 20em;
    max-width: 100%%; /* useful when width is set to anything other than 100%% */
    box-sizing: border-box;
    margin: 0;
    border: 1px solid #aaa;
    box-shadow: 0 1px 0 1px rgba(0,0,0,.04);
    border-radius: .5em;
    -webkit-appearance: none;
    appearance: none;
    background-color:  transparent;
    background-image: url('data:image/svg+xml;charset=US-ASCII,%%3Csvg%%20xmlns%%3D%%22http%%3A%%2F%%2Fwww.w3.org%%2F2000%%2Fsvg%%22%%20width%%3D%%22292.4%%22%%20height%%3D%%22292.4%%22%%3E%%3Cpath%%20fill%%3D%%22%%23007CB2%%22%%20d%%3D%%22M287%%2069.4a17.6%%2017.6%%200%%200%%200-13-5.4H18.4c-5%%200-9.3%%201.8-12.9%%205.4A17.6%%2017.6%%200%%200%%200%%200%%2082.2c0%%205%%201.8%%209.3%%205.4%%2012.9l128%%20127.9c3.6%%203.6%%207.8%%205.4%%2012.8%%205.4s9.2-1.8%%2012.8-5.4L287%%2095c3.5-3.5%%205.4-7.8%%205.4-12.8%%200-5-1.9-9.2-5.5-12.8z%%22%%2F%%3E%%3C%%2Fsvg%%3E'),
      linear-gradient(to bottom, #ffffff 0%%,#e5e5e5 100%%);
    background-repeat: no-repeat, repeat;
    /* arrow icon position (1em from the right, 50%% vertical) , then gradient position*/
    background-position: right .7em top 50%%, 0 0;
    /* icon size, then gradient */
    background-size: .65em auto, 100%%;
}
/* Hide arrow icon in IE browsers */
select::-ms-expand {
    display: none;
}
    </style>
</head>

<body>
    <br><br>
    <table border="0">
        <tr>
            <td align="left">
                <label class="switch">
                    <input type="checkbox" %CHECKED% onclick="checkOnOff()" id="OnOff">
                    <span class="btnslider round"></span>
                </label>
            </td>

            <td style="vertical-align:text-bottom">
                <!--
                <p style="vertical-align:bottom"><span style="vertical-align:text-bottom" id="txtOnOff">%ANAUS%</span></p>
-->
            </td>
        <tr>
            <td>&nbsp;</td>
        </tr>
        <tr>
            <th colspan="1" align="left">Helligkeit</th>
        </tr>
        <tr>
            <td><input type="range" onchange="updateSliderBrightness(this)" id="brightnessSlider" min="5" max="100"
                    value="%DIMVALUE%" step="1" class="slider"></p>
            </td>
            <td>&nbsp;</td>
            <td align="right">
                <p><span id="textSliderValue">%DIMVALUE%</span></p>
            </td>
        </tr>
        <tr>
            <th colspan="1" align="left">Farbtemperatur</th>
        </tr>
        <td><input type="range" onchange="updateSliderCt(this)" id="CtSlider" min="250" max="417" value="%CTVALUE%"
                step="1" class="slider"></p>
        </td>
        <td>&nbsp;</td>
        <td align="right">
            <p><span id="textCtSliderValue">%CTVALUE%</span></p>
        </td>
        </tr>
    </table>

  %SCENES%

    <script>
        function checkOnOff() {
            var checkbox = document.getElementById('OnOff');
            var result = "OFF";
            var text = "Aus";
            if (checkbox.checked == true) {
                result = "ON";
                text = "An";
            }
            var xhr = new XMLHttpRequest();
            xhr.open("GET", "/cm?cmnd=POWER " + result, true);
            xhr.send();
        }
        function updateSliderBrightness(element) {
            var sliderValue = document.getElementById("brightnessSlider").value;
            document.getElementById("textSliderValue").innerHTML = sliderValue;
            console.log(sliderValue);
            var xhr = new XMLHttpRequest();
            xhr.open("GET", "/cm?cmnd=DIMMER " + sliderValue, true);
            xhr.send();
        }
        function updateSliderCt(element) {
            var sliderValue = document.getElementById("CtSlider").value;
            document.getElementById("textCtSliderValue").innerHTML = sliderValue;
            console.log(sliderValue);
            var xhr = new XMLHttpRequest();
            xhr.open("GET", "/cm?cmnd=CT " + sliderValue, true);
            xhr.send();
        }
        function updateScene(element) {
            var sliderValue = document.getElementById("sceneselect").value;
            console.log(sliderValue);
            var xhr = new XMLHttpRequest();
            xhr.open("GET", "/cm?cmnd=SCENE " + sliderValue, true);
            xhr.send();
        }        
    </script>
</body>

</html>
)rawliteral";