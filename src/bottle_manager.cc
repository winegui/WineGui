/**
 * Copyright (c) 2019 WineGUI
 *
 * \file    bottle_manager.cc
 * \brief   The controller controls it all
 * \author  Melroy van den Berg <webmaster1989@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "bottle_manager.h"
#include "main_window.h"
#include "signal_dispatcher.h"
#include "helper.h"
#include "wine_defaults.h"

#include <chrono>
#include <stdexcept>

/**
 * \brief Constructor
 * \param mainWindow Address to the main Window
 */
BottleManager::BottleManager(MainWindow& mainWindow):
  m_Mutex(),
  mainWindow(mainWindow),
  activeBottle(nullptr),
  error_message()
{
  // TODO: Make it configurable via settings
  std::vector<std::string> dirs{Glib::get_home_dir(), ".winegui", "prefixes"};
  BOTTLE_LOCATION = Glib::build_path(G_DIR_SEPARATOR_S, dirs);
}

/**
 * \brief Destructor
 */
BottleManager::~BottleManager() {}

/**
 * \brief Prepare method, called during initial start-up of the app
 */
void BottleManager::Prepare()
{
  // Install winetricks if not yet present,
  // Winetricks script is used by WineGUI.
  if(!Helper::FileExists(Helper::GetWinetricksLocation()))
  {
    try {
      Helper::InstallOrUpdateWinetricks();
    }
    catch(const std::runtime_error& error)
    {
      mainWindow.ShowErrorMessage(error.what());
    }
  }
  else
  {
    // Update existing script
    try {
      Helper::SelfUpdateWinetricks();
    }
    catch(const std::invalid_argument& msg)
    {
      std::cout << "WARN: " << msg.what() << std::endl;
    }
    catch(const std::runtime_error& error)
    {
      mainWindow.ShowErrorMessage(error.what());
    }
  }

  // Start the initial read from disk to fetch the bottles & update GUI
  UpdateBottles();
}

/**
 * \brief Update bottles by reading the Wine Bottles from disk and update GUI 
 */
void BottleManager::UpdateBottles()
{
  // Clear bottles
  if(bottles.size() > 0)
    bottles.clear();

  // Get the bottle directories
  std::map<string, unsigned long> bottleDirs;
  try {
    bottleDirs = GetBottlePaths();
  }
  catch(const std::runtime_error& error)
  {
    mainWindow.ShowErrorMessage(error.what());
    return; // stop
  }

  if(bottleDirs.size() > 0) {
    try {
      // Create wine bottles from bottle directories and wine version
      bottles = CreateWineBottles(GetWineVersion(), bottleDirs);
    }
    catch(const std::runtime_error& error)
    {
      mainWindow.ShowErrorMessage(error.what());
      return; // stop
    } 
  
    if(bottles.size() > 0)
    {
      // Update main Window
      mainWindow.SetWineBottles(bottles);

      // Set first Bottle in the detailed info panel,
      // begin() gives you an iterator
      auto first = bottles.begin();
      mainWindow.SetDetailedInfo(*first);
      // Set active bottle at the first
      this->activeBottle = &(*first);
    }
    else
    {
      mainWindow.ShowErrorMessage("Could not create an overview of Windows Machines. Empty list.");
      // Set active bottle to NULL
      this->activeBottle = nullptr;
    }
  }
  else
  {
    // Set active bottle to NULL
    this->activeBottle = nullptr;
  }
}

/**
 * \brief Create a new Wine Bottle (runs in thread!)
 * \param[in] caller                      - Signal Dispatcher pointer, in order to signal back events
 * \param[in] name                        - Bottle Name
 * \param[in] virtual_desktop_resolution  - Virtual desktop resolution (empty if disabled)
 * \param[in] windows_version             - Windows OS version
 * \param[in] bit                         - Windows Bit (32/64-bit)
 * \param[in] audio                       - Audio Driver type
 */
void BottleManager::NewBottle(
    SignalDispatcher* caller,
    Glib::ustring name,
    Glib::ustring virtual_desktop_resolution,
    BottleTypes::Windows windows_version,
    BottleTypes::Bit bit,
    BottleTypes::AudioDriver audio)
{
  // Calculate prefix
  std::vector<std::string> dirs{BOTTLE_LOCATION, name};
  auto wine_prefix = Glib::build_path(G_DIR_SEPARATOR_S, dirs);
  bool bottle_created = false;
  try {
    // First create a new Wine Bottle
    Helper::CreateWineBottle(wine_prefix, bit);
    
    bottle_created = true;
  }
  catch (const std::runtime_error& error)
  {
    {
      std::lock_guard<std::mutex> lock(m_Mutex);
      error_message = ("Something went wrong during creation of a new Windows machine!\n" + 
        Glib::ustring(error.what()));
    }
    caller->SignalErrorMessage();
    return; // Stop thread
  }

  // Continue with additional settings
  if(bottle_created)
  {
    // Only change Windows OS when NOT default
    if(windows_version != WineDefaults::WINDOWS_OS)
    {
      try {
        Helper::SetWindowsVersion(wine_prefix, windows_version);
      }
      catch (const std::runtime_error& error)
      {
        {
          std::lock_guard<std::mutex> lock(m_Mutex);
          error_message = ("Something went wrong during setting another Windows version.\n" + 
            Glib::ustring(error.what()));
        }
        caller->SignalErrorMessage();
        return; // Stop thread
      } 
    }

    // Only if virtual desktop is not empty, enable it
    if(!virtual_desktop_resolution.empty())
    {
      try {
        Helper::SetVirtualDesktop(wine_prefix, virtual_desktop_resolution);
      }
      catch (const std::runtime_error& error)
      {
        {
          std::lock_guard<std::mutex> lock(m_Mutex);
          error_message = ("Something went wrong during enabling virtual desktop mode.\n" + 
            Glib::ustring(error.what()));
        }
        caller->SignalErrorMessage();
        return; // Stop thread
      } 
    }

    // Only if Audio driver is not default, change it
    if(audio != WineDefaults::AUDIO_DRIVER)
    {
      try {
        Helper::SetAudioDriver(wine_prefix, audio);
      }
      catch (const std::runtime_error& error)
      {
        {
          std::lock_guard<std::mutex> lock(m_Mutex);
          error_message = ("Something went wrong during setting another audio driver.\n" + 
            Glib::ustring(error.what()));
        }
        caller->SignalErrorMessage();
        return; // Stop thread
      } 
    }

    // TODO: Finally add name to WineGUI config file
  }

  // Before we send a finish signal, wait until the status is OK, with a time-out.
  // Especially needed when the user create a default Windows OS Wine bottle, with no additional settings
  int time_out_counter = 0;
  while(!Helper::GetBottleStatus(wine_prefix) && time_out_counter < 10)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ++time_out_counter;
  }
  caller->SignalBottleCreated();
}

/**
 * \brief Remove the current active Wine bottle
 */
void BottleManager::DeleteBottle()
{
  if(activeBottle != nullptr)
  {
    try
    {
      Glib::ustring prefix_path = activeBottle->wine_location();
      string windows = BottleTypes::toString(activeBottle->windows());
      // Are you sure?'
      if(mainWindow.ShowConfirmDialog("Are you sure you want to *permanently* remove machine named '" + Helper::GetName(prefix_path) + "' running " + windows + "?\n\nNote: This action cannot be undone!"))
      {
        Helper::RemoveWineBottle(prefix_path);
        this->UpdateBottles();
      }
      else {
        // Nothing, canceled
      }
    }
    catch (const std::runtime_error& error)
    {
      mainWindow.ShowErrorMessage(error.what());
    } 
  }
  else
  {
    mainWindow.ShowErrorMessage("No Windows Machine to remove, empty/no selection.");
  }
}

/**
 * \brief Get error message (stored from manager thread)
 * \return Return the error message
 */
const Glib::ustring& BottleManager::GetErrorMessage()
{
  std::lock_guard<std::mutex> lock(m_Mutex);
  return error_message; 
}

/**
 * \brief Run a program in Wine (using the active selected bottle)
 * \param[in] filename - Filename location of the program, selected by user
 * \param[in] is_msi_file - Is the program you try to run a Windows Installer (MSI)?
 */
void BottleManager::RunProgram(string filename, bool is_msi_file)
{
  // Check if there is an active bottle set
  if(activeBottle != nullptr)
  {
    Glib::ustring wine_prefix = activeBottle->wine_location();
    std::thread t(&Helper::RunProgram, wine_prefix, filename, is_msi_file); // false = EXE
    t.detach(); 
  }
  else
  {
    mainWindow.ShowErrorMessage("No Windows Machine selected/empty. First create a new machine!\n\nAborted.");
  }
}

/**
 * \brief Update active bottle
 */
void BottleManager::SetActiveBottle(BottleItem* bottle)
{
  if(bottle != nullptr)
  {
    this->activeBottle = bottle;
  }
}

/**
 * \brief Get Wine version
 * \return Wine version
 */
string BottleManager::GetWineVersion()
{
  // Read wine version (is always the same for all bottles atm)
  string wineVersion = "";
  try
  {
    wineVersion = Helper::GetWineVersion();
  }
  catch (const std::runtime_error& error)
  {
    mainWindow.ShowErrorMessage(error.what());
  }
  return wineVersion;
}

/**
 * \brief Get Bottle Paths
 * \return Return a map of bottle paths (string) and modification time (in ms)
 */
std::map<string, unsigned long> BottleManager::GetBottlePaths()
{
  if(!Helper::DirExists(BOTTLE_LOCATION)) {
    // Create directory if not exist yet
    if(g_mkdir_with_parents(BOTTLE_LOCATION.c_str(), 0775) < 0 && errno != EEXIST) {
      throw std::runtime_error("Failed to create WineGUI directory: " + BOTTLE_LOCATION + " (" + g_strerror(errno) + ")");
    }
  }
  if(Helper::DirExists(BOTTLE_LOCATION)) {
    // Continue
    return Helper::GetBottlesPaths(BOTTLE_LOCATION);
  }
  else
  {
    throw std::runtime_error("Configuration directory not found (could not create):\n" + BOTTLE_LOCATION);
  }
  // Otherwise empty
  return std::map<string, unsigned long>();
}

/**
 * \brief Create wine bottle classes and add them to the private bottles variable
 * \param[in] wineVersion The current wine version used
 * \param[in] bottleDirs  The list of bottle directories
 */
std::list<BottleItem> BottleManager::CreateWineBottles(string wineVersion, std::map<string, unsigned long> bottleDirs)
{
  std::list<BottleItem> bottles;

  string name = "";
  string virtualDesktop = BottleTypes::VIRTUAL_DESKTOP_DISABLED;
  bool status = false;
  BottleTypes::Windows windows = BottleTypes::Windows::WindowsXP;
  BottleTypes::Bit bit = BottleTypes::Bit::win32;
  string cDriveLocation = "";
  string lastTimeWineUpdated = "";
  BottleTypes::AudioDriver audioDriver = BottleTypes::AudioDriver::pulseaudio;
  
  // Retrieve detailed info for each wine bottle prefix
  for (const auto &[prefix, _]: bottleDirs ) {
    std::ignore = _;
    // Reset variables
    name = "";
    virtualDesktop = BottleTypes::VIRTUAL_DESKTOP_DISABLED;
    status = false;
    windows = BottleTypes::Windows::WindowsXP;
    bit = BottleTypes::Bit::win32;
    cDriveLocation = "- Unknown -";
    lastTimeWineUpdated = "- Unknown -";
    audioDriver = BottleTypes::AudioDriver::pulseaudio;

    try {
      name = Helper::GetName(prefix);
      virtualDesktop = Helper::GetVirtualDesktop(prefix);
      status = Helper::GetBottleStatus(prefix);
      windows = Helper::GetWindowsOSVersion(prefix);
      bit = Helper::GetSystemBit(prefix);
      cDriveLocation = Helper::GetCLetterDrive(prefix);
      lastTimeWineUpdated = Helper::GetLastWineUpdated(prefix);
      audioDriver = Helper::GetAudioDriver(prefix);
    }
    catch (const std::runtime_error& error)
    {
      mainWindow.ShowErrorMessage(error.what());
    }

    BottleItem* bottle = new BottleItem(
      name,
      status,
      windows,
      bit,
      wineVersion,
      prefix,
      cDriveLocation,
      lastTimeWineUpdated,
      audioDriver,
      virtualDesktop);
    bottles.push_back(*bottle);
  }
  return bottles;
}
