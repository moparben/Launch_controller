<pre> ```mermaid flowchart TD subgraph Base_Layer["Base Layer (Bottom Deck)"] Sensors[Left Board<br/>Sensors & Inputs<br/>(DHT11, Thermistors, Wind, CAN)] Logic[Center Board<br/>ESP32, Display, Web, Settings] Power[Right Board<br/>MOSFETs, ACS712,<br/>Fans, Heatsink] end %% External connections Battery[LiPo / Power Module] Ignitors[Ignitor Outputs] Fans[Fans (Rear/Top Exhaust)] Display[TFT Display<br/>Touch (Front Panel)] CAN[CAN Bus] SensorsExt[External Sensors] %% Connections Battery -- "Power (V+/GND)" --> Power Battery -- "Logic Power" --> Logic Power -- "Control Signals" --> Logic Power -- "Status Signals" --> Logic Power -- "Current Sense" --> Logic Power -- "Ignitor Wires" --> Ignitors Power -- "Fan Power" --> Fans Power -- "Heatsink Mount" --- Heatsink[Heatsink Exposed<br/>on Box Bottom] Logic -- "Sensor Bus" --> Sensors Sensors -- "Sensor Data" --> Logic Logic -- "Display SPI/I2C" --> Display Sensors -- "CAN Bus" --> CAN Sensors -- "External Sensors" --> SensorsExt ``` </pre>
4. View the Rendered Diagram
After saving/committing, open the file (diagram.md or README.md) in your repo.
GitHub will automatically render the Mermaid diagram for you!
If You Need to Undo or Edit
You can always click the pencil (edit) icon to update, or the history tab to revert changes.
Let me know if you want a screenshot or have trouble at any step!
Once you’re comfortable with this, you’ll be able to add, edit, or move diagrams and notes anywhere in your project.

How can I help you?

