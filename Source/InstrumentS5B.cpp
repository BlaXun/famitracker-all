/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2014  Jonathan Liss
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful, 
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
** Library General Public License for more details.  To obtain a 
** copy of the GNU Library General Public License, write to the Free 
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
*/

#include "stdafx.h"
#include "FamiTrackerDoc.h"
#include "Instrument.h"
#include "DocumentFile.h"

const int CInstrumentS5B::SEQUENCE_TYPES[] = {SEQ_VOLUME, SEQ_ARPEGGIO, SEQ_PITCH, SEQ_HIPITCH, SEQ_DUTYCYCLE};

CInstrumentS5B::CInstrumentS5B()
{
	for (int i = 0; i < SEQUENCE_COUNT; ++i) {
		m_iSeqEnable[i] = 0;
		m_iSeqIndex[i] = 0;
	}
}

CInstrument *CInstrumentS5B::Clone() const
{
	CInstrumentS5B *pNew = new CInstrumentS5B();

	for (int i = 0; i < SEQUENCE_COUNT; i++) {
		pNew->SetSeqEnable(i, GetSeqEnable(i));
		pNew->SetSeqIndex(i, GetSeqIndex(i));
	}

	pNew->SetName(GetName());

	return pNew;
}

void CInstrumentS5B::Setup()
{
}

void CInstrumentS5B::Store(CDocumentFile *pDocFile)
{
	pDocFile->WriteBlockInt(SEQUENCE_COUNT);

	for (int i = 0; i < SEQUENCE_COUNT; i++) {
		pDocFile->WriteBlockChar(GetSeqEnable(i));
		pDocFile->WriteBlockChar(GetSeqIndex(i));
	}
}

bool CInstrumentS5B::Load(CDocumentFile *pDocFile)
{
	int SeqCnt = pDocFile->GetBlockInt();

	ASSERT_FILE_DATA(SeqCnt < (SEQUENCE_COUNT + 1));

	SeqCnt = SEQUENCE_COUNT;

	for (int i = 0; i < SeqCnt; i++) {
		SetSeqEnable(i, pDocFile->GetBlockChar());
		int Index = pDocFile->GetBlockChar();
		ASSERT_FILE_DATA(Index < MAX_SEQUENCES);
		SetSeqIndex(i, Index);
	}

	return true;
}

void CInstrumentS5B::SaveFile(CInstrumentFile *pFile, const CFamiTrackerDoc *pDoc)
{
	// Sequences, as the VRC6's (and 0CC-FamiTracker's 5B instruments)
	pFile->WriteChar(SEQUENCE_COUNT);

	for (int i = 0; i < SEQUENCE_COUNT; ++i) {
		const CSequence *pSeq = GetSeqEnable(i) ? pDoc->GetSequenceS5B(GetSeqIndex(i), i) : NULL;
		if (pSeq != NULL) {
			pFile->WriteChar(1);
			pFile->WriteInt(pSeq->GetItemCount());
			pFile->WriteInt(pSeq->GetLoopPoint());
			pFile->WriteInt(pSeq->GetReleasePoint());
			pFile->WriteInt(pSeq->GetSetting());

			for (unsigned j = 0; j < pSeq->GetItemCount(); ++j) {
				pFile->WriteChar(pSeq->GetItem(j));
			}
		}
		else {
			pFile->WriteChar(0);
		}
	}
}

bool CInstrumentS5B::LoadFile(CInstrumentFile *pFile, int iVersion, CFamiTrackerDoc *pDoc)
{
	unsigned char SeqCount = pFile->ReadChar();

	if (SeqCount > SEQUENCE_COUNT)
		return false;

	for (int i = 0; i < SeqCount; ++i) {
		unsigned char Enabled = pFile->ReadChar();
		if (Enabled == 1) {
			int Count = pFile->ReadInt();
			if (Count < 0 || Count > MAX_SEQUENCE_ITEMS)
				return false;
			int Index = pDoc->GetFreeSequenceS5B(i);
			CSequence Read;
			Read.SetItemCount(Count);
			Read.SetLoopPoint(pFile->ReadInt());
			Read.SetReleasePoint(pFile->ReadInt());
			Read.SetSetting(pFile->ReadInt());
			for (int j = 0; j < Count; ++j)
				Read.SetItem(j, pFile->ReadChar());
			if (Index != -1) {
				pDoc->GetSequence(SNDCHIP_S5B, Index, i)->Copy(&Read);
				SetSeqEnable(i, true);
				SetSeqIndex(i, Index);
			}
		}
		else {
			SetSeqEnable(i, false);
			SetSeqIndex(i, 0);
		}
	}

	return true;
}

int CInstrumentS5B::Compile(CFamiTrackerDoc *pDoc, CChunk *pChunk, int Index)
{
	return 0;	// no NSF driver plays the 5B here: CCompiler refuses such modules
}

bool CInstrumentS5B::CanRelease() const
{
	if (GetSeqEnable(SEQ_VOLUME) != 0) {
		const CSequence *pSeq = CFamiTrackerDoc::GetDoc()->GetSequenceS5B(GetSeqIndex(SEQ_VOLUME), SEQ_VOLUME);
		return pSeq != NULL && pSeq->GetReleasePoint() != -1;
	}

	return false;
}

int	CInstrumentS5B::GetSeqEnable(int Index) const
{
	return m_iSeqEnable[Index];
}

int	CInstrumentS5B::GetSeqIndex(int Index) const
{
	return m_iSeqIndex[Index];
}

void CInstrumentS5B::SetSeqEnable(int Index, int Value)
{
	m_iSeqEnable[Index] = Value;
}

void CInstrumentS5B::SetSeqIndex(int Index, int Value)
{
	m_iSeqIndex[Index] = Value;
}
