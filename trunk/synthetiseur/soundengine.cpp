/***************************************************************************
**                                                                        **
**  Polyphone, a soundfont editor                                         **
**  Copyright (C) 2013-2014 Davy Triponney                                **
**                                                                        **
**  This program is free software: you can redistribute it and/or modify  **
**  it under the terms of the GNU General Public License as published by  **
**  the Free Software Foundation, either version 3 of the License, or     **
**  (at your option) any later version.                                   **
**                                                                        **
**  This program is distributed in the hope that it will be useful,       **
**  but WITHOUT ANY WARRANTY; without even the implied warranty of        **
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         **
**  GNU General Public License for more details.                          **
**                                                                        **
**  You should have received a copy of the GNU General Public License     **
**  along with this program.  If not, see http://www.gnu.org/licenses/.   **
**                                                                        **
****************************************************************************
**           Author: Davy Triponney                                       **
**  Website/Contact: http://www.polyphone.fr/                             **
**             Date: 01.01.2013                                           **
***************************************************************************/

#include "soundengine.h"
#include <QThread>

// Variable statique
QList<SoundEngine*> SoundEngine::_listInstances = QList<SoundEngine*>();
int SoundEngine::_gainSmpl = 0;
bool SoundEngine::_isStereo = false;
bool SoundEngine::_isSinusEnabled = false;
bool SoundEngine::_isLoopEnabled = true;

SoundEngine::SoundEngine(QObject *parent) : CircularBuffer(BUFFER_ENGINE_MIN_DATA, BUFFER_ENGINE_MAX_DATA, parent)
{
    _listInstances << this;
    _dataTmpL = new float [4 * BUFFER_ENGINE_MAX_DATA];
    _dataTmpR = new float [4 * BUFFER_ENGINE_MAX_DATA];
}

SoundEngine::~SoundEngine()
{
    _listInstances.removeOne(this);
    delete [] _dataTmpL;
    delete [] _dataTmpR;
}

int SoundEngine::getNbVoices()
{
    _mutexVoices.lock();
    int iRet = _listVoices.size();
    _mutexVoices.unlock();
    return iRet;
}

void SoundEngine::addVoice(Voice * voice, QList<Voice*> friends)
{
    // Exclusive class ?
    int exclusiveClass = voice->getExclusiveClass();
    if (exclusiveClass != 0)
        closeAll(exclusiveClass, voice->getPresetNumber(), friends);

    // Recherche du soundengine le moins surchargé
    int index = -1;
    int minVoiceNumber = -1;
    for (int i = 0; i < _listInstances.size(); i++)
    {
        int nbVoices = _listInstances.at(i)->getNbVoices();
        if (minVoiceNumber == -1 || nbVoices < minVoiceNumber)
        {
            index = i;
            minVoiceNumber = nbVoices;
        }
    }
    if (index != -1)
        _listInstances.at(index)->addVoiceInstance(voice);
}

// Ajout de voix pour l'écoute d'un sample (1 ou 2 canaux + sinus)
void SoundEngine::addVoices(QList<Voice *> voices)
{
    int nbInstances = _listInstances.size();
    for (int i = 0; i < voices.size(); i++)
    {
        if (voices.at(i)->getNote() == -3)
        {
            if (_isSinusEnabled)
                voices.at(i)->attackToMax();
            else
                voices.at(i)->decayToMin();
        }
        else
            voices.at(i)->setLoopMode(_isLoopEnabled);
        _listInstances.at(i % nbInstances)->addVoiceInstance(voices.at(i));
        _listInstances.at(i % nbInstances)->setStereoInstance(_isStereo);
    }
}

void SoundEngine::addVoiceInstance(Voice * voice)
{
    _mutexVoices.lock();
    _listVoices << voice;
    _mutexVoices.unlock();
}

void SoundEngine::stopAllVoices()
{
    for (int i = 0; i < _listInstances.size(); i++)
        _listInstances.at(i)->stopAllVoicesInstance();
}

void SoundEngine::stopAllVoicesInstance()
{
    _mutexVoices.lock();
    while (!_listVoices.isEmpty())
    {
        if (_listVoices.last()->getNote() == -1)
            emit(readFinished());
        delete _listVoices.takeLast();
    }
    _mutexVoices.unlock();
}

void SoundEngine::releaseNote(int numNote)
{
    for (int i = 0; i < _listInstances.size(); i++)
        _listInstances.at(i)->releaseNoteInstance(numNote);
}

void SoundEngine::releaseNoteInstance(int numNote)
{
    _mutexVoices.lock();
    if (numNote == -1)
    {
        // Arrêt lecture d'un sample
        for (int i = 0; i < _listVoices.size(); i++)
            if (_listVoices.at(i)->getNote() < 0)
                _listVoices.at(i)->release();
    }
    else
    {
        for (int i = 0; i < _listVoices.size(); i++)
            if (_listVoices.at(i)->getNote() == numNote)
                _listVoices.at(i)->release();
    }
    _mutexVoices.unlock();
}

void SoundEngine::setGain(double gain)
{
    for (int i = 0; i < _listInstances.size(); i++)
        _listInstances.at(i)->setGainInstance(gain);
}

void SoundEngine::setGainInstance(double gain)
{
    _mutexVoices.lock();
    for (int i = 0; i < _listVoices.size(); i++)
        if (_listVoices.at(i)->getNote() >= 0)
            _listVoices.at(i)->setGain(gain);
    _mutexVoices.unlock();
}

void SoundEngine::setChorus(int level, int depth, int frequency)
{
    for (int i = 0; i < _listInstances.size(); i++)
        _listInstances.at(i)->setChorusInstance(level, depth, frequency);
}

void SoundEngine::setChorusInstance(int level, int depth, int frequency)
{
    _mutexVoices.lock();
    for (int i = 0; i < _listVoices.size(); i++)
        if (_listVoices.at(i)->getNote() >= 0)
            _listVoices.at(i)->setChorus(level, depth, frequency);
    _mutexVoices.unlock();
}

void SoundEngine::setRootKey(int rootKey)
{
    for (int i = 0; i < _listInstances.size(); i++)
        _listInstances.at(i)->setRootKeyInstance(rootKey);
}

void SoundEngine::setRootKeyInstance(int rootKey)
{
    // mise à jour voix -3 (sinus)
    _mutexVoices.lock();
    for (int i = 0; i < _listVoices.size(); i++)
        if (_listVoices.at(i)->getNote() == -3)
            _listVoices.at(i)->setRootKey(rootKey);
    _mutexVoices.unlock();
}

void SoundEngine::setPitchCorrection(int correction, bool repercute)
{
    for (int i = 0; i < _listInstances.size(); i++)
        _listInstances.at(i)->setPitchCorrectionInstance(correction, repercute);
}

void SoundEngine::setPitchCorrectionInstance(int correction, bool repercute)
{
    _mutexVoices.lock();
    for (int i = 0; i < _listVoices.size(); i++)
        if (_listVoices.at(i)->getNote() == -1 || (_listVoices.at(i)->getNote() == -2 && repercute))
            _listVoices[i]->setFineTune(correction);
    _mutexVoices.unlock();
}

void SoundEngine::setStartLoop(int startLoop, bool repercute)
{
    for (int i = 0; i < _listInstances.size(); i++)
        _listInstances.at(i)->setStartLoopInstance(startLoop, repercute);
}

void SoundEngine::setStartLoopInstance(int startLoop, bool repercute)
{
    _mutexVoices.lock();
    for (int i = 0; i < _listVoices.size(); i++)
        if (_listVoices.at(i)->getNote() == -1 || (_listVoices.at(i)->getNote() == -2 && repercute))
            _listVoices[i]->setLoopStart(startLoop);
    _mutexVoices.unlock();
}

void SoundEngine::setEndLoop(int endLoop, bool repercute)
{
    for (int i = 0; i < _listInstances.size(); i++)
        _listInstances.at(i)->setEndLoopInstance(endLoop, repercute);
}

void SoundEngine::setEndLoopInstance(int endLoop, bool repercute)
{
    _mutexVoices.lock();
    for (int i = 0; i < _listVoices.size(); i++)
        if (_listVoices.at(i)->getNote() == -1 || (_listVoices.at(i)->getNote() == -2 && repercute))
            _listVoices[i]->setLoopEnd(endLoop);
    _mutexVoices.unlock();
}

void SoundEngine::setSinusEnabled(bool isEnabled)
{
    _isSinusEnabled = isEnabled;
    for (int i = 0; i < _listInstances.size(); i++)
        _listInstances.at(i)->setSinusEnabledInstance(isEnabled);
}

void SoundEngine::setSinusEnabledInstance(bool isEnabled)
{
    // Mise à jour voix -3
    _mutexVoices.lock();
    for (int i = 0; i < _listVoices.size(); i++)
    {
        if (_listVoices.at(i)->getNote() == -3)
        {
            if (isEnabled)
                _listVoices.at(i)->attackToMax();
            else
                _listVoices.at(i)->decayToMin();
        }
    }
    _mutexVoices.unlock();
}

void SoundEngine::setLoopEnabled(bool isEnabled)
{
    _isLoopEnabled = isEnabled;
    for (int i = 0; i < _listInstances.size(); i++)
        _listInstances.at(i)->setLoopEnabledInstance(isEnabled);
}

void SoundEngine::setLoopEnabledInstance(bool isEnabled)
{
    // Mise a jour voix -1 et -2
    _mutexVoices.lock();
    for (int i = 0; i < _listVoices.size(); i++)
        if (_listVoices.at(i)->getNote() == -1 || _listVoices.at(i)->getNote() == -2)
            _listVoices.at(i)->setLoopMode(isEnabled);
    _mutexVoices.unlock();
}

void SoundEngine::setStereo(bool isStereo)
{
    _isStereo = isStereo;
    for (int i = 0; i < _listInstances.size(); i++)
        _listInstances.at(i)->setStereoInstance(isStereo);
}

void SoundEngine::setStereoInstance(bool isStereo)
{
    // Mise à jour voix -1 et -2
    _mutexVoices.lock();
    Voice * voice1 = NULL;
    Voice * voice2 = NULL;
    for (int i = 0; i < _listVoices.size(); i++)
    {
        if (_listVoices.at(i)->getNote() == -1)
            voice1 = _listVoices.at(i);
        else if (_listVoices.at(i)->getNote() == -2)
            voice2 = _listVoices.at(i);
    }
    if (isStereo)
    {
        if (voice1)
        {
            double pan = voice1->getPan();
            if (pan < 0)
                voice1->setPan(-50);
            else if (pan > 0)
                voice1->setPan(50);
            if (voice2)
                voice1->setGain(_gainSmpl - 12);
            else
                voice1->setGain(_gainSmpl);
        }
        if (voice2)
            voice2->setGain(_gainSmpl - 12);
    }
    else
    {
        if (voice1)
        {
            double pan = voice1->getPan();
            if (pan < 0)
                voice1->setPan(-1);
            else if (pan > 0)
                voice1->setPan(1);
            voice1->setGain(_gainSmpl);
        }
        if (voice2)
            voice2->setGain(-1000);
    }
    _mutexVoices.unlock();
}

void SoundEngine::setGainSample(int gain)
{
    _gainSmpl = gain;
    for (int i = 0; i < _listInstances.size(); i++)
        _listInstances.at(i)->setGainSampleInstance(gain);
}

void SoundEngine::setGainSampleInstance(int gain)
{
    // Modification du gain des samples, mise à jour voix -1 et -2
    _mutexVoices.lock();
    for (int i = 0; i < _listVoices.size(); i++)
    {
        if (_listVoices.at(i)->getNote() == -1)
        {
            if (_isStereo)
                _listVoices.at(i)->setGain(gain - 12);
            else
                _listVoices.at(i)->setGain(gain);
        }
        else if (_listVoices.at(i)->getNote() == -2 && _isStereo)
            _listVoices.at(i)->setGain(gain - 12);
    }
    _mutexVoices.unlock();
}

void SoundEngine::closeAll(int exclusiveClass, int numPreset, QList<Voice*> friends)
{
    for (int i = 0; i < _listInstances.size(); i++)
        _listInstances.at(i)->closeAllInstance(exclusiveClass, numPreset, friends);
}

void SoundEngine::closeAllInstance(int exclusiveClass, int numPreset, QList<Voice*> friends)
{
    _mutexVoices.lock();
    for (int i = 0; i < _listVoices.size(); i++)
    {
        if (_listVoices.at(i)->getExclusiveClass() == exclusiveClass &&
                _listVoices.at(i)->getPresetNumber() == numPreset &&
                !friends.contains(_listVoices.at(i)))
            _listVoices.at(i)->release(true);
    }
    _mutexVoices.unlock();
}
