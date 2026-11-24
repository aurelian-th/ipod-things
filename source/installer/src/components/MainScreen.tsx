import type React from "react";
import { useHeadingFocus } from "../hooks/useHeadingFocus";
import { ChoiceButton } from "./ui/ChoiceButton";
import { InstallerScreen } from "./ui/InstallerScreen";

interface IMainScreenProps {
  onInstall: () => void;
  onMigrate: () => void;
  onStore: () => void;
  onUninstall: () => void;
  installDisabled?: boolean;
  updates: React.ReactNode;
}

const MainScreen: React.FC<IMainScreenProps> = ({
  onInstall,
  onMigrate,
  onStore,
  onUninstall,
  installDisabled,
  updates,
}) => {
  const { headingRef } = useHeadingFocus();

  return (
    <InstallerScreen.Root aria-labelledby="main-title">
      <InstallerScreen.Title id="main-title" ref={headingRef} tabIndex={-1}>
        RevisionOS
      </InstallerScreen.Title>
      <InstallerScreen.Description className="mt-3 max-w-135 text-[14px] leading-6">
        Install RevisionOS on your iPod Classic, pick extensions and homebrew, or update an
        existing device in Disk Mode.
      </InstallerScreen.Description>
      <InstallerScreen.Actions className="mx-auto mt-8 max-w-116 flex-col gap-3">
        <ChoiceButton
          title="Install or reapply RevisionOS"
          description="Set up core firmware, extensions, games, and apps from Apple 2.0.5 firmware."
          onClick={onInstall}
          disabled={installDisabled}
        />
        <ChoiceButton
          title="Migrate from RepriseOS or RevolaOS"
          description="Detect existing RepriseOS or RevolaOS installs, move files to /Revola/, and import selections."
          onClick={onMigrate}
          disabled={installDisabled}
        />
        <ChoiceButton
          title="Store and updates"
          description="Fetch GitHub catalog updates, see what is new, and install or remove extensions and homebrew."
          onClick={onStore}
          disabled={installDisabled}
        />
        <ChoiceButton
          title="Uninstall or restore"
          description="Remove RevisionOS files in Disk Mode, view NOR backups, or restore stock Apple firmware."
          onClick={onUninstall}
          disabled={installDisabled}
        />
      </InstallerScreen.Actions>
      <div className="installer-well mx-auto mt-6 max-w-135 space-y-2 rounded-lg p-4 text-left text-[12px] leading-5 text-body">
        <p>
          <strong className="font-bold text-ink">Temporary RevolaOS backbone:</strong>{" "}
          RevisionOS currently runs on the RevolaOS core and bootloader backbone. Folders and files
          on your iPod use <code className="text-ink">/Revola/</code> paths (such as{" "}
          <code className="text-ink">/Revola/Firmware/revola.bin</code> and{" "}
          <code className="text-ink">/Revola/Extensions/</code>) until upstream RepriseOS implements
          all required features.
        </p>
        <p>
          <strong className="font-bold text-ink">Custom firmware build required:</strong> Extensions
          such as Hold Standby Clock, Silent Remote Volume, Car Audio, and Lyrics require the custom
          RevisionOS <code className="text-ink">revola.bin</code> build rather than upstream
          RevolaOS or RepriseOS binaries.
        </p>
      </div>
      {updates}
    </InstallerScreen.Root>
  );
};

export { MainScreen };
