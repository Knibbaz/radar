import { useEffect, useState } from 'react';
import RadarCanvas from './components/RadarCanvas';
import Hud from './components/Hud';
import DetailCard from './components/DetailCard';
import SettingsPanel from './components/SettingsPanel';
import { useSettings } from './hooks/useSettings';
import { useAircraftFeed } from './hooks/useAircraftFeed';

export default function App() {
  const { settings, setSettings } = useSettings();
  const { storeRef, pollMsRef, status } = useAircraftFeed(settings);
  const [selectedHex, setSelectedHex] = useState<string | null>(null);

  // drop the selection when the aircraft leaves the feed
  useEffect(() => {
    if (selectedHex && !storeRef.current.has(selectedHex)) setSelectedHex(null);
  }, [status.pollTick, selectedHex, storeRef]);

  const selected = selectedHex ? storeRef.current.get(selectedHex) ?? null : null;

  return (
    <div className="app">
      <div className="scope-wrap">
        <RadarCanvas
          settings={settings}
          storeRef={storeRef}
          pollMsRef={pollMsRef}
          selectedHex={selectedHex}
          onSelect={setSelectedHex}
        />
        <Hud
          count={status.count}
          feedOk={status.lastPollOk}
          rangeKm={settings.rangeKm}
          onRange={(rangeKm) => setSettings({ rangeKm })}
        />
      </div>
      <div className="panel">
        <DetailCard aircraft={selected} settings={settings} />
      </div>
      <SettingsPanel settings={settings} onSave={setSettings} />
    </div>
  );
}
